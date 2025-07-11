/*
    Copyright (C) 1998 Lars Knoll (knoll@mpi-hd.mpg.de)
    Copyright (C) 2001 Dirk Mueller <mueller@kde.org>
    Copyright (C) 2004-2025 Apple Inc. All rights reserved.
    Copyright (C) 2010 Google Inc. All rights reserved.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
 */

#pragma once

#include <WebCore/FrameLoaderTypes.h>
#include <WebCore/LoaderStrategy.h>
#include <WebCore/ResourceLoadPriority.h>
#include <WebCore/ResourceLoaderOptions.h>
#include <WebCore/Timer.h>
#include <array>
#include <wtf/CheckedPtr.h>
#include <wtf/Deque.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/WeakPtr.h>
#include <wtf/text/StringHash.h>
#include <wtf/text/WTFString.h>

class WebResourceLoadScheduler;

WebResourceLoadScheduler& webResourceLoadScheduler();

class WebResourceLoadScheduler final : public WebCore::LoaderStrategy, public CanMakeCheckedPtr<WebResourceLoadScheduler> {
    WTF_MAKE_TZONE_ALLOCATED(WebResourceLoadScheduler);
    WTF_MAKE_NONCOPYABLE(WebResourceLoadScheduler);
    WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(WebResourceLoadScheduler);
public:
    WebResourceLoadScheduler();

    void loadResource(WebCore::LocalFrame&, WebCore::CachedResource&, WebCore::ResourceRequest&&, const WebCore::ResourceLoaderOptions&, CompletionHandler<void(RefPtr<WebCore::SubresourceLoader>&&)>&&) final;
    void loadResourceSynchronously(WebCore::FrameLoader&, WebCore::ResourceLoaderIdentifier, const WebCore::ResourceRequest&, WebCore::ClientCredentialPolicy, const WebCore::FetchOptions&, const WebCore::HTTPHeaderMap&, WebCore::ResourceError&, WebCore::ResourceResponse&, Vector<uint8_t>&) final;
    void pageLoadCompleted(WebCore::Page&) final;
    void browsingContextRemoved(WebCore::LocalFrame&) final;

    void remove(WebCore::ResourceLoader*) final;
    void setDefersLoading(WebCore::ResourceLoader&, bool) final;
    void crossOriginRedirectReceived(WebCore::ResourceLoader*, const URL& redirectURL) final;
    
    void servePendingRequests(WebCore::ResourceLoadPriority minimumPriority = WebCore::ResourceLoadPriority::VeryLow) final;
    void suspendPendingRequests() final;
    void resumePendingRequests() final;

    void startPingLoad(WebCore::LocalFrame&, WebCore::ResourceRequest&, const WebCore::HTTPHeaderMap&, const WebCore::FetchOptions&, WebCore::ContentSecurityPolicyImposition, PingLoadCompletionHandler&&) final;

    void preconnectTo(WebCore::FrameLoader&, WebCore::ResourceRequest&&, WebCore::StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&&) final;

    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }

    bool isSerialLoadingEnabled() const { return m_isSerialLoadingEnabled; }
    void setSerialLoadingEnabled(bool b) { m_isSerialLoadingEnabled = b; }

    void schedulePluginStreamLoad(WebCore::LocalFrame&, WebCore::NetscapePlugInStreamLoaderClient&, WebCore::ResourceRequest&&, CompletionHandler<void(RefPtr<WebCore::NetscapePlugInStreamLoader>&&)>&&);

    bool isOnLine() const final;
    void addOnlineStateChangeListener(WTF::Function<void(bool)>&&) final;

    static WebCore::ResourceError blockedErrorFromRequest(const WebCore::ResourceRequest&);
    static WebCore::ResourceError pluginWillHandleLoadErrorFromResponse(const WebCore::ResourceResponse&);

private:
    virtual ~WebResourceLoadScheduler();

    void scheduleLoad(WebCore::ResourceLoader*);
    void scheduleServePendingRequests();
    void requestTimerFired();

    WebCore::ResourceError cancelledError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError blockedError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError blockedByContentBlockerError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError cannotShowURLError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError interruptedForPolicyChangeError(const WebCore::ResourceRequest&) const final;
#if ENABLE(CONTENT_FILTERING)
    WebCore::ResourceError blockedByContentFilterError(const WebCore::ResourceRequest&) const final;
#endif
    WebCore::ResourceError cannotShowMIMETypeError(const WebCore::ResourceResponse&) const final;
    WebCore::ResourceError fileDoesNotExistError(const WebCore::ResourceResponse&) const final;
    WebCore::ResourceError httpsUpgradeRedirectLoopError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError httpNavigationWithHTTPSOnlyError(const WebCore::ResourceRequest&) const final;
    WebCore::ResourceError pluginWillHandleLoadError(const WebCore::ResourceResponse&) const final;

    bool isSuspendingPendingRequests() const { return !!m_suspendPendingRequestsCount; }
    void isResourceLoadFinished(WebCore::CachedResource&, CompletionHandler<void(bool)>&&) final;

    class HostInformation final : public CanMakeWeakPtr<HostInformation>, public CanMakeCheckedPtr<HostInformation> {
        WTF_MAKE_NONCOPYABLE(HostInformation);
        WTF_MAKE_TZONE_ALLOCATED(HostInformation);
        WTF_OVERRIDE_DELETE_FOR_CHECKED_PTR(HostInformation);
    public:
        HostInformation(const String&, unsigned);
        ~HostInformation();
        
        const String& name() const { return m_name; }
        void schedule(WebCore::ResourceLoader*, WebCore::ResourceLoadPriority = WebCore::ResourceLoadPriority::VeryLow);
        void addLoadInProgress(WebCore::ResourceLoader*);
        void remove(WebCore::ResourceLoader*);
        bool hasRequests() const;
        bool limitRequests(WebCore::ResourceLoadPriority) const;

        typedef Deque<RefPtr<WebCore::ResourceLoader>> RequestQueue;
        RequestQueue& requestsPending(WebCore::ResourceLoadPriority priority) { return m_requestsPending[priorityToIndex(priority)]; }

    private:
        static unsigned priorityToIndex(WebCore::ResourceLoadPriority);

        std::array<RequestQueue, WebCore::resourceLoadPriorityCount> m_requestsPending;
        typedef HashSet<RefPtr<WebCore::ResourceLoader>> RequestMap;
        RequestMap m_requestsLoading;
        const String m_name;
        const unsigned m_maxRequestsInFlight;
    };

    enum CreateHostPolicy {
        CreateIfNotFound,
        FindOnly
    };
    
    CheckedPtr<HostInformation> hostForURL(const URL&, CreateHostPolicy = FindOnly);
    void servePendingRequests(CheckedRef<HostInformation>&&, WebCore::ResourceLoadPriority);

    typedef HashMap<String, std::unique_ptr<HostInformation>, StringHash> HostMap;
    HostMap m_hosts;
    const UniqueRef<HostInformation> m_nonHTTPProtocolHost;
        
    WebCore::Timer m_requestTimer;

    unsigned m_suspendPendingRequestsCount;
    bool m_isSerialLoadingEnabled;
};
