/*
 * Copyright (C) 2018 mce sys Ltd. All rights reserved.
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

#include "config.h"
#include <wtf/text/ExternalStringImpl.h>

namespace WTF {

WTF_EXPORT_PRIVATE Ref<ExternalStringImpl> ExternalStringImpl::create(std::span<const LChar> characters, ExternalStringImplFreeFunction&& free)
{
    return adoptRef(*new ExternalStringImpl(characters, WTFMove(free)));
}

WTF_EXPORT_PRIVATE Ref<ExternalStringImpl> ExternalStringImpl::create(std::span<const char16_t> characters, ExternalStringImplFreeFunction&& free)
{
    return adoptRef(*new ExternalStringImpl(characters, WTFMove(free)));
}

ExternalStringImpl::ExternalStringImpl(std::span<const LChar> characters, ExternalStringImplFreeFunction&& free)
    : StringImpl(characters, ConstructWithoutCopying)
    , m_free(WTFMove(free))
{
    ASSERT(m_free);
    m_hashAndFlags = (m_hashAndFlags & ~s_hashMaskBufferOwnership) | BufferExternal;
}

ExternalStringImpl::ExternalStringImpl(std::span<const char16_t> characters, ExternalStringImplFreeFunction&& free)
    : StringImpl(characters, ConstructWithoutCopying)
    , m_free(WTFMove(free))
{
    ASSERT(m_free);
    m_hashAndFlags = (m_hashAndFlags & ~s_hashMaskBufferOwnership) | BufferExternal;
}

} // namespace WTF
