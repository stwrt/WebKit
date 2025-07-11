/*
 * Copyright (C) 2010 Apple Inc. All rights reserved.
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
#include "WKString.h"
#include "WKStringPrivate.h"

#include "WKAPICast.h"
#include <JavaScriptCore/InitializeThreading.h>
#include <JavaScriptCore/OpaqueJSString.h>
#include <WebCore/WebCoreJITOperations.h>
#include <wtf/StdLibExtras.h>
#include <wtf/unicode/UTF8Conversion.h>

WKTypeID WKStringGetTypeID()
{
    return WebKit::toAPI(API::String::APIType);
}

WKStringRef WKStringCreateWithUTF8CString(const char* string)
{
    return WebKit::toAPILeakingRef(API::String::create(WTF::String::fromUTF8(string)));
}

WKStringRef WKStringCreateWithUTF8CStringWithLength(const char* string, size_t stringLength)
{
    return WebKit::toAPILeakingRef(API::String::create(WTF::String::fromUTF8(unsafeMakeSpan(string, stringLength))));
}

bool WKStringIsEmpty(WKStringRef stringRef)
{
    return WebKit::toProtectedImpl(stringRef)->stringView().isEmpty();
}

size_t WKStringGetLength(WKStringRef stringRef)
{
    return WebKit::toProtectedImpl(stringRef)->stringView().length();
}

size_t WKStringGetCharacters(WKStringRef stringRef, WKChar* buffer, size_t bufferLength)
{
    static_assert(sizeof(WKChar) == sizeof(char16_t), "Size of WKChar must match size of char16_t");

    unsigned unsignedBufferLength = std::min<size_t>(bufferLength, std::numeric_limits<unsigned>::max());
    auto substring = WebKit::toProtectedImpl(stringRef)->stringView().left(unsignedBufferLength);

    substring.getCharacters(unsafeMakeSpan(reinterpret_cast<char16_t*>(buffer), bufferLength));
    return substring.length();
}

size_t WKStringGetMaximumUTF8CStringSize(WKStringRef stringRef)
{
    return static_cast<size_t>(WebKit::toProtectedImpl(stringRef)->stringView().length()) * 3 + 1;
}

enum StrictType { NonStrict = false, Strict = true };

template <StrictType strict>
size_t WKStringGetUTF8CStringImpl(WKStringRef stringRef, char* buffer, size_t bufferSize)
{
    if (!bufferSize)
        return 0;

    auto string = WebKit::toProtectedImpl(stringRef)->stringView();

    auto target = unsafeMakeSpan(byteCast<char8_t>(buffer), bufferSize);
    WTF::Unicode::ConversionResult<char8_t> result;
    if (string.is8Bit())
        result = WTF::Unicode::convert(string.span8(), target.first(bufferSize - 1));
    else {
        if constexpr (strict == NonStrict)
            result = WTF::Unicode::convertReplacingInvalidSequences(string.span16(), target.first(bufferSize - 1));
        else {
            result = WTF::Unicode::convert(string.span16(), target.first(bufferSize - 1));
            if (result.code == WTF::Unicode::ConversionResultCode::SourceInvalid)
                return 0;
        }
    }

    target[result.buffer.size()] = '\0';
    return result.buffer.size() + 1;
}

size_t WKStringGetUTF8CString(WKStringRef stringRef, char* buffer, size_t bufferSize)
{
    return WKStringGetUTF8CStringImpl<StrictType::Strict>(stringRef, buffer, bufferSize);
}

size_t WKStringGetUTF8CStringNonStrict(WKStringRef stringRef, char* buffer, size_t bufferSize)
{
    return WKStringGetUTF8CStringImpl<StrictType::NonStrict>(stringRef, buffer, bufferSize);
}

bool WKStringIsEqual(WKStringRef aRef, WKStringRef bRef)
{
    return WebKit::toProtectedImpl(aRef)->stringView() == WebKit::toProtectedImpl(bRef)->stringView();
}

bool WKStringIsEqualToUTF8CString(WKStringRef aRef, const char* b)
{
    // FIXME: Should we add a fast path that avoids memory allocation when the string is all ASCII?
    // FIXME: We can do even the general case more efficiently if we write a function in StringView that understands UTF-8 C strings.
    return WebKit::toProtectedImpl(aRef)->stringView() == WTF::String::fromUTF8(b);
}

bool WKStringIsEqualToUTF8CStringIgnoringCase(WKStringRef aRef, const char* b)
{
    // FIXME: Should we add a fast path that avoids memory allocation when the string is all ASCII?
    // FIXME: We can do even the general case more efficiently if we write a function in StringView that understands UTF-8 C strings.
    return equalIgnoringASCIICase(WebKit::toProtectedImpl(aRef)->stringView(), WTF::String::fromUTF8(b));
}

WKStringRef WKStringCreateWithJSString(JSStringRef jsStringRef)
{
    auto apiString = jsStringRef ? API::String::create(jsStringRef->string()) : API::String::createNull();

    return WebKit::toAPILeakingRef(WTFMove(apiString));
}

JSStringRef WKStringCopyJSString(WKStringRef stringRef)
{
    JSC::initialize();
    WebCore::populateJITOperations();
    return OpaqueJSString::tryCreate(WebKit::toProtectedImpl(stringRef)->string()).leakRef();
}
