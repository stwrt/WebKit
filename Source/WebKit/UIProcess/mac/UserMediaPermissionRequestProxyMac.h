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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "UserMediaPermissionRequestProxy.h"
#include <WebCore/SecurityOrigin.h>

namespace WebKit {

class UserMediaPermissionRequestProxyMac final : public UserMediaPermissionRequestProxy {
public:
    ~UserMediaPermissionRequestProxyMac() final;

    UserMediaPermissionRequestProxyMac(UserMediaPermissionRequestManagerProxy&, std::optional<WebCore::UserMediaRequestIdentifier>, WebCore::FrameIdentifier mainFrameID, FrameInfoData&&, Ref<WebCore::SecurityOrigin>&& userMediaDocumentOrigin, Ref<WebCore::SecurityOrigin>&& topLevelDocumentOrigin, Vector<WebCore::CaptureDevice>&& audioDevices, Vector<WebCore::CaptureDevice>&& videoDevices, WebCore::MediaStreamRequest&&, CompletionHandler<void(bool)>&&);

private:
    void promptForGetDisplayMedia(UserMediaDisplayCapturePromptType) final;
    bool canRequestDisplayCapturePermission() final;
    void invalidate() final;

#if ENABLE(MEDIA_STREAM)
    bool m_hasPendingGetDisplayMediaPrompt { false };
#endif
};


} // namespace WebKit
