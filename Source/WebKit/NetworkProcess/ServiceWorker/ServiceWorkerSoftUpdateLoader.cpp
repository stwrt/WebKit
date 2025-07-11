/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ServiceWorkerSoftUpdateLoader.h"

#include "Logging.h"
#include "NetworkCache.h"
#include "NetworkLoad.h"
#include "NetworkSession.h"
#include <WebCore/AdvancedPrivacyProtections.h>
#include <WebCore/HTTPStatusCodes.h>
#include <WebCore/ServiceWorkerJob.h>
#include <WebCore/TextResourceDecoder.h>
#include <WebCore/WorkerFetchResult.h>
#include <WebCore/WorkerScriptLoader.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(ServiceWorkerSoftUpdateLoader);

ServiceWorkerSoftUpdateLoader::ServiceWorkerSoftUpdateLoader(NetworkSession& session, ServiceWorkerJobData&& jobData, bool shouldRefreshCache, ResourceRequest&& request, Handler&& completionHandler)
    : m_completionHandler(WTFMove(completionHandler))
    , m_jobData(WTFMove(jobData))
    , m_session(session)
{
    ASSERT(!request.isConditional());

    if (RefPtr cache = session.cache()) {
        // We set cache policy to disable speculative loading/async revalidation from the cache.
        request.setCachePolicy(ResourceRequestCachePolicy::ReturnCacheDataDontLoad);

        OptionSet<AdvancedPrivacyProtections> advancedPrivacyProtections;
        bool allowPrivacyProxy { true };
        cache->retrieve(request, std::nullopt, NavigatingToAppBoundDomain::No, allowPrivacyProxy, advancedPrivacyProtections, [this, weakThis = WeakPtr { *this }, request, shouldRefreshCache](auto&& entry, auto&&) mutable {
            if (!weakThis)
                return;
            if (!m_session) {
                fail(ResourceError { ResourceError::Type::Cancellation });
                return;
            }
            if (!shouldRefreshCache && entry && !entry->needsValidation()) {
                loadWithCacheEntry(*entry);
                return;
            }

            request.setCachePolicy(ResourceRequestCachePolicy::RefreshAnyCacheData);
            if (entry) {
                m_cacheEntry = WTFMove(entry);

                String eTag = m_cacheEntry->response().httpHeaderField(HTTPHeaderName::ETag);
                if (!eTag.isEmpty())
                    request.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, eTag);

                String lastModified = m_cacheEntry->response().httpHeaderField(HTTPHeaderName::LastModified);
                if (!lastModified.isEmpty())
                    request.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, lastModified);
            }
            loadFromNetwork(CheckedRef { *m_session }.get(), WTFMove(request));
        });
        return;
    }
    loadFromNetwork(session, WTFMove(request));
}

ServiceWorkerSoftUpdateLoader::~ServiceWorkerSoftUpdateLoader()
{
    if (m_completionHandler)
        m_completionHandler(workerFetchError(ResourceError { ResourceError::Type::Cancellation }));
}

void ServiceWorkerSoftUpdateLoader::fail(ResourceError&& error)
{
    if (!m_completionHandler)
        return;

    m_completionHandler(workerFetchError(WTFMove(error)));
    didComplete();
}

void ServiceWorkerSoftUpdateLoader::loadWithCacheEntry(NetworkCache::Entry& entry)
{
    auto error = processResponse(entry.response());
    if (!error.isNull()) {
        fail(WTFMove(error));
        return;
    }

    if (RefPtr buffer = entry.buffer())
        didReceiveBuffer(*buffer);
    didFinishLoading({ });
}

void ServiceWorkerSoftUpdateLoader::loadFromNetwork(NetworkSession& session, ResourceRequest&& request)
{
    NetworkLoadParameters parameters;
    parameters.storedCredentialsPolicy = StoredCredentialsPolicy::Use;
    parameters.contentSniffingPolicy = ContentSniffingPolicy::DoNotSniffContent;
    parameters.contentEncodingSniffingPolicy = ContentEncodingSniffingPolicy::Default;
    parameters.needsCertificateInfo = true;
    parameters.request = WTFMove(request);
    Ref networkLoad = NetworkLoad::create(*this, WTFMove(parameters), session);
    m_networkLoad = networkLoad.copyRef();
    networkLoad->start();

#if PLATFORM(COCOA)
    session.appPrivacyReportTestingData().setDidPerformSoftUpdate();
#endif
}

void ServiceWorkerSoftUpdateLoader::willSendRedirectedRequest(ResourceRequest&&, ResourceRequest&&, ResourceResponse&&, CompletionHandler<void(WebCore::ResourceRequest&&)>&& completionHandler)
{
    fail(ResourceError { ResourceError::Type::Cancellation });
    completionHandler({ }); // May deallocate this ServiceWorkerSoftUpdateLoader.
}

void ServiceWorkerSoftUpdateLoader::didReceiveResponse(ResourceResponse&& response, PrivateRelayed, ResponseCompletionHandler&& completionHandler)
{
    m_certificateInfo = *response.certificateInfo();
    if (response.httpStatusCode() == httpStatus304NotModified && m_cacheEntry) {
        loadWithCacheEntry(*m_cacheEntry);
        completionHandler(PolicyAction::Ignore);
        return;
    }
    auto error = processResponse(response);
    if (!error.isNull()) {
        fail(WTFMove(error));
        completionHandler(PolicyAction::Ignore);
        return;
    }
    completionHandler(PolicyAction::Use);
}

// https://w3c.github.io/ServiceWorker/#update-algorithm, steps 9.7 to 9.17
ResourceError ServiceWorkerSoftUpdateLoader::processResponse(const ResourceResponse& response)
{
    auto source = m_jobData.workerType == WorkerType::Module ? WorkerScriptLoader::Source::ModuleScript : WorkerScriptLoader::Source::ClassicWorkerScript;
    auto error = WorkerScriptLoader::validateWorkerResponse(response, source, FetchOptions::Destination::Serviceworker);
    if (!error.isNull())
        return error;

    error = ServiceWorkerJob::validateServiceWorkerResponse(m_jobData, response);
    if (!error.isNull())
        return error;

    m_contentSecurityPolicy = ContentSecurityPolicyResponseHeaders { response };
    // Service workers are always secure contexts.
    m_crossOriginEmbedderPolicy = obtainCrossOriginEmbedderPolicy(response, nullptr);
    m_referrerPolicy = response.httpHeaderField(HTTPHeaderName::ReferrerPolicy);
    m_responseEncoding = response.textEncodingName();

    return { };
}

void ServiceWorkerSoftUpdateLoader::didReceiveBuffer(const WebCore::FragmentedSharedBuffer& buffer)
{
    if (!m_decoder) {
        if (!m_responseEncoding.isEmpty())
            m_decoder = TextResourceDecoder::create("text/javascript"_s, m_responseEncoding);
        else
            m_decoder = TextResourceDecoder::create("text/javascript"_s, "UTF-8"_s);
    }

    buffer.forEachSegment([&](auto segment) {
        if (segment.size())
            m_script.append(m_decoder->decode(segment));
    });
}

void ServiceWorkerSoftUpdateLoader::didFinishLoading(const WebCore::NetworkLoadMetrics&)
{
    if (RefPtr decoder = m_decoder)
        m_script.append(decoder->flush());
    m_completionHandler({ ScriptBuffer { m_script.toString() }, m_jobData.scriptURL, m_certificateInfo, m_contentSecurityPolicy, m_crossOriginEmbedderPolicy, m_referrerPolicy, { } });
    didComplete();
}

void ServiceWorkerSoftUpdateLoader::didFailLoading(const ResourceError& error)
{
    fail(ResourceError(error));
}

void ServiceWorkerSoftUpdateLoader::didComplete()
{
    m_networkLoad = nullptr;
    if (CheckedPtr session = m_session.get())
        session->removeSoftUpdateLoader(this);
}

} // namespace WebKit
