/*
 * Copyright (C) 2021-2025 Apple Inc. All rights reserved.
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

#include "WebGPUCompositorIntegration.h"
#include <optional>
#include <wtf/MachSendRight.h>
#include <wtf/Ref.h>
#include <wtf/RefCounted.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
namespace WebGPU {
class Device;

enum class TextureFormat : uint8_t;
}

class DestinationColorSpace;
class ImageBuffer;

class GPUCompositorIntegration : public RefCounted<GPUCompositorIntegration> {
public:
    static Ref<GPUCompositorIntegration> create(Ref<WebGPU::CompositorIntegration>&& backing)
    {
        return adoptRef(*new GPUCompositorIntegration(WTFMove(backing)));
    }

#if PLATFORM(COCOA)
    Vector<MachSendRight> recreateRenderBuffers(int width, int height, WebCore::DestinationColorSpace&&, WebCore::AlphaPremultiplication, WebCore::WebGPU::TextureFormat, WebCore::WebGPU::Device&) const;
#endif

    void prepareForDisplay(uint32_t frameIndex, CompletionHandler<void()>&&);

    WebGPU::CompositorIntegration& backing() { return m_backing; }
    const WebGPU::CompositorIntegration& backing() const { return m_backing; }

    void paintCompositedResultsToCanvas(WebCore::ImageBuffer&, uint32_t);
    void updateContentsHeadroom(float);

private:
    GPUCompositorIntegration(Ref<WebGPU::CompositorIntegration>&& backing)
        : m_backing(WTFMove(backing))
    {
    }

    const Ref<WebGPU::CompositorIntegration> m_backing;
};

}
