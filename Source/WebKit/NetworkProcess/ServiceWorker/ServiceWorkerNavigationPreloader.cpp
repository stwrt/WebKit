/*
 * Copyright (C) 2021 Apple Inc. All rights reserved.
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
#include "ServiceWorkerNavigationPreloader.h"

#include "DownloadManager.h"
#include "Logging.h"
#include "NetworkCache.h"
#include "NetworkLoad.h"
#include "NetworkSession.h"
#include "PrivateRelayed.h"
#include <WebCore/HTTPStatusCodes.h>
#include <WebCore/NavigationPreloadState.h>
#include <wtf/TZoneMallocInlines.h>

namespace WebKit {

using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(ServiceWorkerNavigationPreloader);

ServiceWorkerNavigationPreloader::ServiceWorkerNavigationPreloader(NetworkSession& session, NetworkLoadParameters&& parameters, const WebCore::NavigationPreloadState& state, bool shouldCaptureExtraNetworkLoadMetric)
    : m_session(session)
    , m_parameters(WTFMove(parameters))
    , m_state(state)
    , m_shouldCaptureExtraNetworkLoadMetrics(shouldCaptureExtraNetworkLoadMetrics())
    , m_startTime(MonotonicTime::now())
{
    RELEASE_LOG(ServiceWorker, "ServiceWorkerNavigationPreloader::ServiceWorkerNavigationPreloader %p", this);
    start();
}

void ServiceWorkerNavigationPreloader::start()
{
    if (m_isStarted)
        return;
    m_isStarted = true;

    CheckedPtr session = m_session.get();
    if (!session) {
        didFailLoading(ResourceError { errorDomainWebKitInternal, 0, { }, "No session for preload"_s });
        return;
    }

    if (RefPtr cache = session->cache()) {
        NetworkCache::GlobalFrameID globalID { *m_parameters.webPageProxyID, *m_parameters.webPageID, *m_parameters.webFrameID };
        cache->retrieve(m_parameters.request, globalID, m_parameters.isNavigatingToAppBoundDomain, m_parameters.allowPrivacyProxy, m_parameters.advancedPrivacyProtections, [weakThis = WeakPtr { *this }](auto&& entry, auto&&) mutable {
            CheckedPtr checkedThis = weakThis.get();
            if (!checkedThis || checkedThis->m_isCancelled)
                return;

            if (entry && !entry->needsValidation()) {
                checkedThis->loadWithCacheEntry(*entry);
                return;
            }

            checkedThis->m_parameters.request.setCachePolicy(ResourceRequestCachePolicy::RefreshAnyCacheData);
            if (entry) {
                checkedThis->m_cacheEntry = WTFMove(entry);

                auto eTag = checkedThis->m_cacheEntry->response().httpHeaderField(HTTPHeaderName::ETag);
                if (!eTag.isEmpty())
                    checkedThis->m_parameters.request.setHTTPHeaderField(HTTPHeaderName::IfNoneMatch, eTag);

                auto lastModified = checkedThis->m_cacheEntry->response().httpHeaderField(HTTPHeaderName::LastModified);
                if (!lastModified.isEmpty())
                    checkedThis->m_parameters.request.setHTTPHeaderField(HTTPHeaderName::IfModifiedSince, lastModified);
            }

            if (!checkedThis->m_session) {
                checkedThis->didFailLoading(ResourceError { ResourceError::Type::Cancellation });
                return;
            }
            checkedThis->loadFromNetwork();
        });
        return;
    }
    loadFromNetwork();
}

ServiceWorkerNavigationPreloader::~ServiceWorkerNavigationPreloader() = default;

void ServiceWorkerNavigationPreloader::cancel()
{
    m_isCancelled = true;
    if (m_responseCompletionHandler)
        m_responseCompletionHandler(PolicyAction::Ignore);
    if (RefPtr networkLoad = m_networkLoad)
        networkLoad->cancel();
}

void ServiceWorkerNavigationPreloader::loadWithCacheEntry(NetworkCache::Entry& entry)
{
    didReceiveResponse(ResourceResponse { entry.response() }, PrivateRelayed::No, [body = RefPtr { entry.buffer() }, weakThis = WeakPtr { *this }](auto) mutable {
        if (!weakThis || weakThis->m_isCancelled)
            return;

        if (body) {
            weakThis->didReceiveBuffer(body.releaseNonNull());
            if (!weakThis)
                return;
        }

        NetworkLoadMetrics networkLoadMetrics;
        networkLoadMetrics.markComplete();
        networkLoadMetrics.responseBodyBytesReceived = 0;
        networkLoadMetrics.responseBodyDecodedSize = 0;
        if (weakThis->shouldCaptureExtraNetworkLoadMetrics()) {
            auto additionalMetrics = WebCore::AdditionalNetworkLoadMetricsForWebInspector::create();
            additionalMetrics->requestHeaderBytesSent = 0;
            additionalMetrics->requestBodyBytesSent = 0;
            additionalMetrics->responseHeaderBytesReceived = 0;
            networkLoadMetrics.additionalNetworkLoadMetricsForWebInspector = WTFMove(additionalMetrics);
        }
        weakThis->didFinishLoading(networkLoadMetrics);
    });
    didComplete();
}

void ServiceWorkerNavigationPreloader::loadFromNetwork()
{
    ASSERT(m_session);
    RELEASE_LOG(ServiceWorker, "ServiceWorkerNavigationPreloader::loadFromNetwork %p", this);

    if (m_state.enabled)
        m_parameters.request.addHTTPHeaderField(HTTPHeaderName::ServiceWorkerNavigationPreload, m_state.headerValue);

    Ref networkLoad = NetworkLoad::create(*this, WTFMove(m_parameters), CheckedRef { *m_session });
    m_networkLoad = networkLoad.copyRef();
    networkLoad->start();
}

void ServiceWorkerNavigationPreloader::willSendRedirectedRequest(ResourceRequest&&, ResourceRequest&&, ResourceResponse&& response, CompletionHandler<void(WebCore::ResourceRequest&&)>&& completionHandler)
{
    didReceiveResponse(WTFMove(response), PrivateRelayed::No, [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)](auto) mutable {
        completionHandler({ });
        if (weakThis)
            weakThis->didComplete();
    });
}

void ServiceWorkerNavigationPreloader::didReceiveResponse(ResourceResponse&& response, PrivateRelayed, ResponseCompletionHandler&& completionHandler)
{
    RELEASE_LOG(ServiceWorker, "ServiceWorkerNavigationPreloader::didReceiveResponse %p", this);

    m_didReceiveResponseOrError = true;

    if (response.isRedirection())
        response.setTainting(ResourceResponse::Tainting::Opaqueredirect);

    if (response.httpStatusCode() == httpStatus304NotModified && m_cacheEntry) {
        auto cacheEntry = WTFMove(m_cacheEntry);
        loadWithCacheEntry(*cacheEntry);
        completionHandler(PolicyAction::Ignore);
        return;
    }

    m_response = WTFMove(response);
    m_response.setRedirected(false);
    m_responseCompletionHandler = WTFMove(completionHandler);

    if (auto callback = std::exchange(m_responseCallback, { }))
        callback();
}

void ServiceWorkerNavigationPreloader::didReceiveBuffer(const FragmentedSharedBuffer& buffer)
{
    if (m_bodyCallback)
        m_bodyCallback(RefPtr { &buffer });
}

void ServiceWorkerNavigationPreloader::didFinishLoading(const NetworkLoadMetrics& networkLoadMetrics)
{
    RELEASE_LOG(ServiceWorker, "ServiceWorkerNavigationPreloader::didFinishLoading %p", this);

    m_networkLoadMetrics = networkLoadMetrics;
    didComplete();
}

void ServiceWorkerNavigationPreloader::didFailLoading(const ResourceError& error)
{
    RELEASE_LOG(ServiceWorker, "ServiceWorkerNavigationPreloader::didFailLoading %p", this);

    m_didReceiveResponseOrError = true;
    m_error = error;
    didComplete();
}

void ServiceWorkerNavigationPreloader::didComplete()
{
    m_networkLoad = nullptr;

    auto responseCallback = std::exchange(m_responseCallback, { });
    auto bodyCallback = std::exchange(m_bodyCallback, { });

    // After calling responseCallback or bodyCallback, |this| might be destroyed.
    if (responseCallback)
        responseCallback();

    if (bodyCallback)
        bodyCallback({ });
}

void ServiceWorkerNavigationPreloader::waitForResponse(ResponseCallback&& callback)
{
    if (!m_error.isNull()) {
        callback();
        return;
    }

    if (m_responseCompletionHandler) {
        callback();
        return;
    }

    m_responseCallback = WTFMove(callback);
}

void ServiceWorkerNavigationPreloader::waitForBody(BodyCallback&& callback)
{
    if (!m_error.isNull() || !m_responseCompletionHandler) {
        callback({ });
        return;
    }

    ASSERT(!m_response.isNull());
    m_bodyCallback = WTFMove(callback);
    m_responseCompletionHandler(PolicyAction::Use);
}

bool ServiceWorkerNavigationPreloader::convertToDownload(DownloadManager& manager, DownloadID downloadID, const WebCore::ResourceRequest& request, const WebCore::ResourceResponse& response)
{
    if (!m_networkLoad)
        return false;

    auto networkLoad = std::exchange(m_networkLoad, nullptr);
    manager.convertNetworkLoadToDownload(downloadID, networkLoad.releaseNonNull(), WTFMove(m_responseCompletionHandler), { }, request, response);
    return true;
}

} // namespace WebKit
