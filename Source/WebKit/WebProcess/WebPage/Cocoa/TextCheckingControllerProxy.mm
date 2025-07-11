/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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

#import "config.h"
#import "TextCheckingControllerProxy.h"

#if ENABLE(PLATFORM_DRIVEN_TEXT_CHECKING)

#import "ArgumentCoders.h"
#import "TextCheckingControllerProxyMessages.h"
#import "WebPage.h"
#import "WebProcess.h"
#import <WebCore/AttributedString.h>
#import <WebCore/BoundaryPointInlines.h>
#import <WebCore/DocumentMarker.h>
#import <WebCore/DocumentMarkerController.h>
#import <WebCore/Editing.h>
#import <WebCore/Editor.h>
#import <WebCore/FocusController.h>
#import <WebCore/RenderObject.h>
#import <WebCore/RenderedDocumentMarker.h>
#import <WebCore/TextIterator.h>
#import <WebCore/VisibleUnits.h>
#import <wtf/TZoneMallocInlines.h>

// FIXME: Remove this after rdar://problem/48914153 is resolved.
#if PLATFORM(MACCATALYST)
typedef NS_ENUM(NSInteger, NSSpellingState) {
    NSSpellingStateSpellingFlag = (1 << 0),
    NSSpellingStateGrammarFlag = (1 << 1)
};
#endif

namespace WebKit {
using namespace WebCore;

WTF_MAKE_TZONE_ALLOCATED_IMPL(TextCheckingControllerProxy);

TextCheckingControllerProxy::TextCheckingControllerProxy(WebPage& page)
    : m_page(page)
{
    WebProcess::singleton().addMessageReceiver(Messages::TextCheckingControllerProxy::messageReceiverName(), m_page->identifier(), *this);
}

TextCheckingControllerProxy::~TextCheckingControllerProxy()
{
    WebProcess::singleton().removeMessageReceiver(Messages::TextCheckingControllerProxy::messageReceiverName(), m_page->identifier());
}

void TextCheckingControllerProxy::ref() const
{
    m_page->ref();
}

void TextCheckingControllerProxy::deref() const
{
    m_page->deref();
}

static OptionSet<DocumentMarkerType> relevantMarkerTypes()
{
    return { DocumentMarkerType::PlatformTextChecking, DocumentMarkerType::Spelling, DocumentMarkerType::Grammar };
}

std::optional<TextCheckingControllerProxy::RangeAndOffset> TextCheckingControllerProxy::rangeAndOffsetRelativeToSelection(int64_t offset, uint64_t length)
{
    RefPtr focusedOrMainFrame = m_page->corePage()->focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return std::nullopt;
    auto& frameSelection = focusedOrMainFrame->selection();
    auto selection = frameSelection.selection();

    RefPtr root = frameSelection.rootEditableElementOrDocumentElement();
    if (!root)
        return std::nullopt;

    auto selectionLiveRange = selection.toNormalizedRange();
    if (!selectionLiveRange)
        return std::nullopt;
    auto selectionRange = SimpleRange { *selectionLiveRange };

    auto scope = makeRangeSelectingNodeContents(*root);
    int64_t adjustedStartLocation = characterCount({ scope.start, selectionRange.start }) + offset;
    if (adjustedStartLocation < 0)
        return std::nullopt;
    auto adjustedSelectionCharacterRange = CharacterRange { static_cast<uint64_t>(adjustedStartLocation), length };

    return { { resolveCharacterRange(scope, adjustedSelectionCharacterRange), adjustedSelectionCharacterRange.location } };
}

void TextCheckingControllerProxy::replaceRelativeToSelection(const WebCore::AttributedString& annotatedString, int64_t selectionOffset, uint64_t length, uint64_t relativeReplacementLocation, uint64_t relativeReplacementLength)
{
    RefPtr frame = m_page->corePage()->focusController().focusedOrMainFrame();
    if (!frame)
        return;
    auto& frameSelection = frame->selection();
    if (!frameSelection.selection().isContentEditable())
        return;

    RefPtr root = frameSelection.rootEditableElementOrDocumentElement();
    if (!root)
        return;

    auto rangeAndOffset = rangeAndOffsetRelativeToSelection(selectionOffset, length);
    if (!rangeAndOffset)
        return;
    auto locationInRoot = rangeAndOffset->locationInRoot;

    auto& markers = frame->document()->markers();
    markers.removeMarkers(rangeAndOffset->range, relevantMarkerTypes());

    RetainPtr annotatedNSString = annotatedString.string.createNSString();
    if (relativeReplacementLocation != NSNotFound) {
        if (auto rangeAndOffsetOfReplacement = rangeAndOffsetRelativeToSelection(selectionOffset + relativeReplacementLocation, relativeReplacementLength)) {
            bool restoreSelection = frameSelection.selection().isRange();

            frame->editor().replaceRangeForSpellChecking(rangeAndOffsetOfReplacement->range, [annotatedNSString substringWithRange:NSMakeRange(relativeReplacementLocation, relativeReplacementLength + [annotatedNSString length] - length)]);

            if (restoreSelection) {
                uint64_t selectionLocationToRestore = locationInRoot - selectionOffset;
                if (selectionLocationToRestore > locationInRoot + relativeReplacementLocation + relativeReplacementLength)
                    frameSelection.setSelection(resolveCharacterRange(makeRangeSelectingNodeContents(*root), { selectionLocationToRestore, 0 }));
            }
        }
    }

    [annotatedString.nsAttributedString() enumerateAttributesInRange:NSMakeRange(0, [annotatedNSString length]) options:0 usingBlock:^(NSDictionary<NSAttributedStringKey, id> *attrs, NSRange attributeRange, BOOL *stop) {
        auto attributeCoreRange = resolveCharacterRange(makeRangeSelectingNodeContents(*root), { locationInRoot + attributeRange.location, attributeRange.length });

        [attrs enumerateKeysAndObjectsUsingBlock:^(NSAttributedStringKey key, id value, BOOL *stop) {
            if (![value isKindOfClass:[NSString class]])
                return;
            markers.addMarker(attributeCoreRange, WebCore::DocumentMarkerType::PlatformTextChecking,
                WebCore::DocumentMarker::PlatformTextCheckingData { key, checked_objc_cast<NSString>(value) });

            // FIXME: Switch to constants after rdar://problem/48914153 is resolved.
            if ([key isEqualToString:@"NSSpellingState"]) {
                NSSpellingState spellingState = (NSSpellingState)[value integerValue];
                if (spellingState & NSSpellingStateSpellingFlag)
                    markers.addMarker(attributeCoreRange, DocumentMarkerType::Spelling);
                if (spellingState & NSSpellingStateGrammarFlag) {
                    NSString *userDescription = [attrs objectForKey:@"NSGrammarUserDescription"];
                    markers.addMarker(attributeCoreRange, DocumentMarkerType::Grammar, String { userDescription });
                }
            }
        }];
    }];
}

void TextCheckingControllerProxy::removeAnnotationRelativeToSelection(const String& annotation, int64_t selectionOffset, uint64_t length)
{
    auto rangeAndOffset = rangeAndOffsetRelativeToSelection(selectionOffset, length);
    if (!rangeAndOffset)
        return;

    auto removeCoreSpellingMarkers = annotation == "NSSpellingState"_s;
    auto types = removeCoreSpellingMarkers ? relevantMarkerTypes() : WebCore::DocumentMarkerType::PlatformTextChecking;
    RefPtr focusedOrMainFrame = m_page->corePage()->focusController().focusedOrMainFrame();
    if (!focusedOrMainFrame)
        return;
    RefPtr document = focusedOrMainFrame->document();
    document->markers().filterMarkers(rangeAndOffset->range, [&] (const DocumentMarker& marker) {
        if (!std::holds_alternative<WebCore::DocumentMarker::PlatformTextCheckingData>(marker.data()))
            return FilterMarkerResult::Keep;
        return std::get<WebCore::DocumentMarker::PlatformTextCheckingData>(marker.data()).key == annotation ? FilterMarkerResult::Remove : FilterMarkerResult::Keep;
    }, types);
}

WebCore::AttributedString TextCheckingControllerProxy::annotatedSubstringBetweenPositions(const WebCore::VisiblePosition& start, const WebCore::VisiblePosition& end)
{
    if (!isEditablePosition(start.deepEquivalent())) {
        ASSERT(!isEditablePosition(end.deepEquivalent()));
        return { };
    }

    auto entireRange = makeSimpleRange(start, end);
    if (!entireRange)
        return { };

    auto string = adoptNS([[NSMutableAttributedString alloc] init]);

    for (TextIterator it(*entireRange); !it.atEnd(); it.advance()) {
        if (!it.text().length())
            continue;
        [string appendAttributedString:adoptNS([[NSAttributedString alloc] initWithString:it.text().createNSStringWithoutCopying().get()]).get()];
        auto range = it.range();
        for (auto& marker : range.start.document().markers().markersInRange(range, DocumentMarkerType::PlatformTextChecking)) {
            auto& data = std::get<DocumentMarker::PlatformTextCheckingData>(marker->data());
            auto subrange = resolveCharacterRange(range, { marker->startOffset(), marker->endOffset() - marker->startOffset() });
            auto attributeRange = characterRange(*entireRange, subrange);
            ASSERT(attributeRange.location + attributeRange.length <= [string length]);
            if (attributeRange.location + attributeRange.length <= [string length])
                [string addAttribute:data.key.createNSString().get() value:data.value.createNSString().get() range:WTFMove(attributeRange)];
        }
    }

    return WebCore::AttributedString::fromNSAttributedString(WTFMove(string));
}

} // namespace WebKit

#endif // ENABLE(PLATFORM_DRIVEN_TEXT_CHECKING)
