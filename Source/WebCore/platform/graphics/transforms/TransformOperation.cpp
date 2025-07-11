/*
 * Copyright (C) 2016 Apple Inc. All rights reserved.
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
#include "TransformOperation.h"

#include "IdentityTransformOperation.h"
#include <wtf/text/TextStream.h>

namespace WebCore {

void IdentityTransformOperation::dump(TextStream& ts) const
{
    ts << type();
}

TextStream& operator<<(TextStream& ts, TransformOperation::Type type)
{
    switch (type) {
    case TransformOperation::Type::ScaleX: ts << "scaleX"_s; break;
    case TransformOperation::Type::ScaleY: ts << "scaleY"_s; break;
    case TransformOperation::Type::Scale: ts << "scale"_s; break;
    case TransformOperation::Type::TranslateX: ts << "translateX"_s; break;
    case TransformOperation::Type::TranslateY: ts << "translateY"_s; break;
    case TransformOperation::Type::Translate: ts << "translate"_s; break;
    case TransformOperation::Type::Rotate: ts << "rotate"_s; break;
    case TransformOperation::Type::SkewX: ts << "skewX"_s; break;
    case TransformOperation::Type::SkewY: ts << "skewY"_s; break;
    case TransformOperation::Type::Skew: ts << "skew"_s; break;
    case TransformOperation::Type::Matrix: ts << "matrix"_s; break;
    case TransformOperation::Type::ScaleZ: ts << "scaleX"_s; break;
    case TransformOperation::Type::Scale3D: ts << "scale3d"_s; break;
    case TransformOperation::Type::TranslateZ: ts << "translateZ"_s; break;
    case TransformOperation::Type::Translate3D: ts << "translate3d"_s; break;
    case TransformOperation::Type::RotateX: ts << "rotateX"_s; break;
    case TransformOperation::Type::RotateY: ts << "rotateY"_s; break;
    case TransformOperation::Type::RotateZ: ts << "rotateZ"_s; break;
    case TransformOperation::Type::Rotate3D: ts << "rotate3d"_s; break;
    case TransformOperation::Type::Matrix3D: ts << "matrix3d"_s; break;
    case TransformOperation::Type::Perspective: ts << "perspective"_s; break;
    case TransformOperation::Type::Identity: ts << "identity"_s; break;
    case TransformOperation::Type::None: ts << "none"_s; break;
    }
    
    return ts;
}

TextStream& operator<<(TextStream& ts, const TransformOperation& operation)
{
    operation.dump(ts);
    return ts;
}

std::optional<TransformOperation::Type> TransformOperation::sharedPrimitiveType(Type other) const
{
    // https://drafts.csswg.org/css-transforms-2/#interpolation-of-transform-functions
    // "If both transform functions share a primitive in the two-dimensional space, both transform
    // functions get converted to the two-dimensional primitive. If one or both transform functions
    // are three-dimensional transform functions, the common three-dimensional primitive is used."
    auto type = primitiveType();
    if (type == other)
        return type;
    static constexpr std::array sharedPrimitives {
        std::array { Type::Rotate, Type::Rotate3D },
        std::array { Type::Scale, Type::Scale3D },
        std::array { Type::Translate, Type::Translate3D }
    };
    for (auto typePair : sharedPrimitives) {
        if ((type == typePair[0] || type == typePair[1]) && (other == typePair[0] || other == typePair[1]))
            return typePair[1];
    }
    return std::nullopt;
}

std::optional<TransformOperation::Type> TransformOperation::sharedPrimitiveType(const TransformOperation* other) const
{
    // Blending with a null operation is always supported via blending with identity.
    if (!other)
        return type();

    // In case we have the same type, make sure to preserve it.
    if (other->type() == type())
        return type();

    return sharedPrimitiveType(other->primitiveType());
}

} // namespace WebCore
