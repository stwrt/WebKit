/*
 * Copyright (C) 2013 Apple Inc. All rights reserved.
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

#import "config.h"
#import "APIWebArchive.h"

#if PLATFORM(COCOA)

#import "APIArray.h"
#import "APIData.h"
#import "APIWebArchiveResource.h"
#import <WebCore/LegacyWebArchive.h>
#import <wtf/RetainPtr.h>
#import <wtf/cf/VectorCF.h>

namespace API {
using namespace WebCore;

Ref<WebArchive> WebArchive::create(WebArchiveResource* mainResource, RefPtr<API::Array>&& subresources, RefPtr<API::Array>&& subframeArchives)
{
    return adoptRef(*new WebArchive(mainResource, WTFMove(subresources), WTFMove(subframeArchives)));
}

Ref<WebArchive> WebArchive::create(API::Data* data)
{
    return adoptRef(*new WebArchive(data));
}

Ref<WebArchive> WebArchive::create(RefPtr<LegacyWebArchive>&& legacyWebArchive)
{
    return adoptRef(*new WebArchive(legacyWebArchive.releaseNonNull()));
}

Ref<WebArchive> WebArchive::create(const SimpleRange& range)
{
    return adoptRef(*new WebArchive(LegacyWebArchive::create(range)));
}

WebArchive::WebArchive(WebArchiveResource* mainResource, RefPtr<API::Array>&& subresources, RefPtr<API::Array>&& subframeArchives)
    : m_cachedMainResource(mainResource)
    , m_cachedSubresources(subresources)
    , m_cachedSubframeArchives(subframeArchives)
{
    RefPtr coreMainResource = m_cachedMainResource->coreArchiveResource();

    Vector<Ref<ArchiveResource>> coreArchiveResources(m_cachedSubresources->size(), [&](size_t i) {
        RefPtr resource = m_cachedSubresources->at<WebArchiveResource>(i);
        ASSERT(resource);
        ASSERT(resource->coreArchiveResource());
        return Ref<ArchiveResource> { *resource->coreArchiveResource() };
    });

    Vector<Ref<LegacyWebArchive>> coreSubframeLegacyWebArchives(m_cachedSubframeArchives->size(), [&](size_t i) {
        RefPtr subframeWebArchive = m_cachedSubframeArchives->at<WebArchive>(i);
        ASSERT(subframeWebArchive);
        ASSERT(subframeWebArchive->coreLegacyWebArchive());
        return Ref<LegacyWebArchive> { *subframeWebArchive->coreLegacyWebArchive() };
    });

    lazyInitialize(m_legacyWebArchive, LegacyWebArchive::create(*coreMainResource, WTFMove(coreArchiveResources), WTFMove(coreSubframeLegacyWebArchives)));
}

WebArchive::WebArchive(API::Data* data)
    : m_legacyWebArchive(LegacyWebArchive::create(SharedBuffer::create(data->span()).get()))
{
}

WebArchive::WebArchive(RefPtr<LegacyWebArchive>&& legacyWebArchive)
    : m_legacyWebArchive(legacyWebArchive)
{
}

WebArchive::~WebArchive()
{
}

WebArchiveResource* WebArchive::mainResource()
{
    if (!m_cachedMainResource)
        m_cachedMainResource = WebArchiveResource::create(m_legacyWebArchive->mainResource());
    return m_cachedMainResource.get();
}

API::Array* WebArchive::subresources()
{
    if (!m_cachedSubresources) {
        auto subresources = WTF::map(m_legacyWebArchive->subresources(), [](auto& subresource) -> RefPtr<API::Object> {
            return WebArchiveResource::create(subresource.ptr());
        });
        lazyInitialize(m_cachedSubresources, API::Array::create(WTFMove(subresources)));
    }

    return m_cachedSubresources.get();
}

API::Array* WebArchive::subframeArchives()
{
    if (!m_cachedSubframeArchives) {
        auto subframeWebArchives = WTF::map(m_legacyWebArchive->subframeArchives(), [](auto& subframeArchive) -> RefPtr<API::Object> {
            return WebArchive::create(downcast<LegacyWebArchive>(subframeArchive.ptr()));
        });
        lazyInitialize(m_cachedSubframeArchives, API::Array::create(WTFMove(subframeWebArchives)));
    }

    return m_cachedSubframeArchives.get();
}

Ref<API::Data> WebArchive::data()
{
    RetainPtr rawDataRepresentation = m_legacyWebArchive->rawDataRepresentation();
    auto rawDataSpan = span(rawDataRepresentation.get());
    return API::Data::createWithoutCopying(rawDataSpan, [rawDataRepresentation = WTFMove(rawDataRepresentation)] { });
}

LegacyWebArchive* WebArchive::coreLegacyWebArchive()
{
    return m_legacyWebArchive.get();
}

} // namespace WebKit

#endif // PLATFORM(COCOA)
