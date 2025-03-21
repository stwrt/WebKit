/*
 * Copyright (C) 2022 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS) && ENABLE(VIDEO)
#include "Connection.h"
#include "RemoteVideoFrameProxy.h"
#include "ThreadSafeObjectHeap.h"
#include "WorkQueueMessageReceiver.h"

#if PLATFORM(COCOA)
#include "SharedVideoFrame.h"
#endif

namespace WebCore {
class DestinationColorSpace;
class PixelBufferConformerCV;
class VideoFrame;
}

namespace WebKit {

// Holds references to all VideoFrame instances that are mapped from GPU process to Web process.
class RemoteVideoFrameObjectHeap final : public IPC::WorkQueueMessageReceiver<WTF::DestructionThread::Any> {
public:
    static Ref<RemoteVideoFrameObjectHeap> create(Ref<IPC::Connection>&&);
    ~RemoteVideoFrameObjectHeap();

    void ref() const final { IPC::WorkQueueMessageReceiver<WTF::DestructionThread::Any>::ref(); }
    void deref() const final { IPC::WorkQueueMessageReceiver<WTF::DestructionThread::Any>::deref(); }

    void close();
    RemoteVideoFrameProxy::Properties add(Ref<WebCore::VideoFrame>&&);
    RefPtr<WebCore::VideoFrame> get(RemoteVideoFrameReadReference&&);

    void stopListeningForIPC(Ref<RemoteVideoFrameObjectHeap>&&) { close(); }
    void lowMemoryHandler();

private:
    explicit RemoteVideoFrameObjectHeap(Ref<IPC::Connection>&&);

    Ref<IPC::Connection> protectedConnection() const { return m_connection; }

    // IPC::MessageReceiver overrides.
    void didReceiveMessage(IPC::Connection&, IPC::Decoder&) final;
    bool didReceiveSyncMessage(IPC::Connection&, IPC::Decoder&, UniqueRef<IPC::Encoder>&) final;

    // Messages.
    void releaseVideoFrame(RemoteVideoFrameWriteReference&&);
#if PLATFORM(COCOA)
    void getVideoFrameBuffer(RemoteVideoFrameReadReference&&, bool canSendIOSurface);
    void pixelBuffer(RemoteVideoFrameReadReference&&, CompletionHandler<void(RetainPtr<CVPixelBufferRef>)>&&);
    void convertFrameBuffer(SharedVideoFrame&&, CompletionHandler<void(WebCore::DestinationColorSpace)>&&);
    void setSharedVideoFrameSemaphore(IPC::Semaphore&&);
    void setSharedVideoFrameMemory(WebCore::SharedMemory::Handle&&);

    UniqueRef<WebCore::PixelBufferConformerCV> createPixelConformer();
#endif

    const Ref<IPC::Connection> m_connection;
    IPC::ThreadSafeObjectHeap<RemoteVideoFrameIdentifier, RefPtr<WebCore::VideoFrame>> m_heap;
#if PLATFORM(COCOA)
    SharedVideoFrameWriter m_sharedVideoFrameWriter;
    SharedVideoFrameReader m_sharedVideoFrameReader;
    Lock m_pixelBufferConformerLock;
    std::unique_ptr<WebCore::PixelBufferConformerCV> m_pixelBufferConformer WTF_GUARDED_BY_LOCK(m_pixelBufferConformerLock);
#endif
    bool m_isClosed { false };
};

}
#endif
