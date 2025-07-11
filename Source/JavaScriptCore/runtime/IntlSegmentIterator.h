/*
 * Copyright (C) 2020 Apple Inc. All rights reserved.
 * Copyright (C) 2021-2022 Apple Inc. All rights reserved.
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

#include "IntlSegmenter.h"

namespace JSC {

class IntlSegmentIterator final : public JSNonFinalObject {
public:
    using Base = JSNonFinalObject;

    static constexpr DestructionMode needsDestruction = NeedsDestruction;

    static void destroy(JSCell* cell)
    {
        static_cast<IntlSegmentIterator*>(cell)->IntlSegmentIterator::~IntlSegmentIterator();
    }

    template<typename CellType, SubspaceAccess mode>
    static GCClient::IsoSubspace* subspaceFor(VM& vm)
    {
        return vm.intlSegmentIteratorSpace<mode>();
    }

    static IntlSegmentIterator* create(VM&, Structure*, std::unique_ptr<UBreakIterator, UBreakIteratorDeleter>&&, Box<Vector<char16_t>>, JSString*, IntlSegmenter::Granularity);
    static Structure* createStructure(VM&, JSGlobalObject*, JSValue);

    DECLARE_INFO;

    JSObject* next(JSGlobalObject*);

    DECLARE_VISIT_CHILDREN;

private:
    IntlSegmentIterator(VM&, Structure*, std::unique_ptr<UBreakIterator, UBreakIteratorDeleter>&&, Box<Vector<char16_t>>&&, IntlSegmenter::Granularity, JSString*);

    DECLARE_DEFAULT_FINISH_CREATION;

    std::unique_ptr<UBreakIterator, UBreakIteratorDeleter> m_segmenter;
    Box<Vector<char16_t>> m_buffer;
    WriteBarrier<JSString> m_string;
    IntlSegmenter::Granularity m_granularity;
};

} // namespace JSC
