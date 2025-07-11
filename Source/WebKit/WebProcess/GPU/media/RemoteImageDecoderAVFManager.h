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

#pragma once

#if ENABLE(GPU_PROCESS) && HAVE(AVASSETREADER)

#include "Connection.h"
#include "GPUProcessConnection.h"
#include "MessageReceiver.h"
#include <WebCore/ImageDecoderIdentifier.h>
#include <WebCore/ImageTypes.h>
#include <WebCore/IntSize.h>
#include <WebCore/SharedBuffer.h>
#include <wtf/HashMap.h>
#include <wtf/TZoneMalloc.h>

namespace WebKit {

class RemoteImageDecoderAVF;
class WebProcess;

class RemoteImageDecoderAVFManager final
    : private GPUProcessConnection::Client
    , private IPC::MessageReceiver
    , public ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<RemoteImageDecoderAVFManager> {
    WTF_MAKE_TZONE_ALLOCATED(RemoteImageDecoderAVFManager);
public:
    static Ref<RemoteImageDecoderAVFManager> create();
    virtual ~RemoteImageDecoderAVFManager();

    void deleteRemoteImageDecoder(const WebCore::ImageDecoderIdentifier&);

    void setUseGPUProcess(bool);
    GPUProcessConnection& ensureGPUProcessConnection();

    WTF_ABSTRACT_THREAD_SAFE_REF_COUNTED_AND_CAN_MAKE_WEAK_PTR_IMPL;

private:
    RemoteImageDecoderAVFManager();
    RefPtr<RemoteImageDecoderAVF> createImageDecoder(WebCore::FragmentedSharedBuffer& data, const String& mimeType, WebCore::AlphaOption, WebCore::GammaAndColorProfileOption);

    // GPUProcessConnection::Client.
    void gpuProcessConnectionDidClose(GPUProcessConnection&) final;

    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    void encodedDataStatusChanged(const WebCore::ImageDecoderIdentifier&, uint64_t frameCount, const WebCore::IntSize&, bool hasTrack);

    HashMap<WebCore::ImageDecoderIdentifier, WeakPtr<RemoteImageDecoderAVF>> m_remoteImageDecoders;

    ThreadSafeWeakPtr<GPUProcessConnection> m_gpuProcessConnection;
};

}

#endif
