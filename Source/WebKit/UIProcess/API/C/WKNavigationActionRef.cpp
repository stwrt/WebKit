/*
 * Copyright (C) 2015 Apple Inc. All rights reserved.
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
#include "WKNavigationActionRef.h"

#include "APINavigationAction.h"
#include "WKAPICast.h"

WKTypeID WKNavigationActionGetTypeID()
{
    return WebKit::toAPI(API::NavigationAction::APIType);
}

bool WKNavigationActionShouldPerformDownload(WKNavigationActionRef action)
{
    return WebKit::toImpl(action)->shouldPerformDownload();
}

WKURLRequestRef WKNavigationActionCopyRequest(WKNavigationActionRef action)
{
    return WebKit::toAPILeakingRef(API::URLRequest::create(WebKit::toImpl(action)->request()));
}

bool WKNavigationActionGetShouldOpenExternalSchemes(WKNavigationActionRef action)
{
    return WebKit::toImpl(action)->shouldOpenExternalSchemes();
}

WKFrameInfoRef WKNavigationActionCopyTargetFrameInfo(WKNavigationActionRef action)
{
    if (RefPtr targetFrame = WebKit::toImpl(action)->targetFrame())
        return WebKit::toAPILeakingRef(targetFrame.releaseNonNull());
    return nullptr;
}

WKFrameNavigationType WKNavigationActionGetNavigationType(WKNavigationActionRef action)
{
    return WebKit::toAPI(WebKit::toImpl(action)->navigationType());
}

WK_EXPORT bool WKNavigationActionHasUnconsumedUserGesture(WKNavigationActionRef action)
{
    return WebKit::toImpl(action)->isProcessingUnconsumedUserGesture();
}
