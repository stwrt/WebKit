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

#pragma once

#include "APIObject.h"
#include "WebMouseEvent.h"
#include <wtf/WeakPtr.h>

#if PLATFORM(MAC)
OBJC_CLASS CALayer;
#include <wtf/RetainPtr.h>
#endif

namespace WebCore {
class GraphicsLayer;
}

namespace WebKit {

class WebPage;

class PageBanner : public API::ObjectImpl<API::Object::Type::BundlePageBanner> {
public:
    enum Type {
        NotSet,
        Header,
        Footer
    };

    class Client {
    public:
        virtual ~Client() { }
        virtual bool mouseEvent(PageBanner*, WebEventType, WebMouseEventButton, const WebCore::IntPoint&) = 0;
    };

#if PLATFORM(MAC)
    static Ref<PageBanner> create(CALayer *, int height, std::unique_ptr<Client>&&);
    CALayer *layer();
#endif

    virtual ~PageBanner();

    void addToPage(Type, WebPage*);
    void detachFromPage();

    void hide();
    void showIfHidden();

    bool mouseEvent(const WebMouseEvent&);
    void didChangeDeviceScaleFactor(float scaleFactor);

    void didAddParentLayer(WebCore::GraphicsLayer*);

private:
#if PLATFORM(MAC)
    explicit PageBanner(CALayer *, int height, std::unique_ptr<Client>&&);

    RefPtr<WebPage> protectedWebPage();
#endif

    std::unique_ptr<Client> m_client;

#if PLATFORM(MAC)
    Type m_type = NotSet;
    WeakPtr<WebPage> m_webPage;
    bool m_mouseDownInBanner = false;
    bool m_isHidden = false;

    RetainPtr<CALayer> m_layer;
    int m_height;
#endif
};

} // namespace WebKit

SPECIALIZE_TYPE_TRAITS_BEGIN(WebKit::PageBanner)
static bool isType(const API::Object& object) { return object.type() == API::Object::Type::BundlePageBanner; }
SPECIALIZE_TYPE_TRAITS_END()
