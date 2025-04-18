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

#include "config.h"
#include "KeyboardScroll.h"

#include <wtf/text/TextStream.h>

namespace WebCore {

FloatSize unitVectorForScrollDirection(ScrollDirection direction)
{
    switch (direction) {
    case ScrollDirection::ScrollUp:
        return { 0, -1 };
    case ScrollDirection::ScrollDown:
        return { 0, 1 };
    case ScrollDirection::ScrollLeft:
        return { -1, 0 };
    case ScrollDirection::ScrollRight:
        return { 1, 0 };
    }

    RELEASE_ASSERT_NOT_REACHED();
}

TextStream& operator<<(TextStream& ts, const KeyboardScroll& scrollData)
{
    return ts << "offset="_s << scrollData.offset << " maximumVelocity="_s << scrollData.maximumVelocity << " force="_s << scrollData.force << " granularity="_s << scrollData.granularity << " direction="_s << scrollData.direction;
}

TextStream& operator<<(TextStream& ts, const KeyboardScrollParameters& parameters)
{
    return ts << "springMass="_s << parameters.springMass << " springStiffness="_s << parameters.springStiffness
    << " springDamping=" << parameters.springDamping << " maximumVelocityMultiplier=" << parameters.maximumVelocityMultiplier
    << " timeToMaximumVelocity=" << parameters.timeToMaximumVelocity << " rubberBandForce=" << parameters.rubberBandForce;
}

}
