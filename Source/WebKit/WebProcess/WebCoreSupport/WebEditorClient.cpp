/*
 * Copyright (C) 2010-2017 Apple Inc. All rights reserved.
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
#include "WebEditorClient.h"

#include "APIInjectedBundleEditorClient.h"
#include "APIInjectedBundleFormClient.h"
#include "EditorState.h"
#include "MessageSenderInlines.h"
#include "SharedBufferReference.h"
#include "UndoOrRedo.h"
#include "WKBundlePageEditorClient.h"
#include "WebFrame.h"
#include "WebPage.h"
#include "WebPageProxy.h"
#include "WebPageProxyMessages.h"
#include "WebProcess.h"
#include "WebUndoStep.h"
#include <WebCore/ArchiveResource.h>
#include <WebCore/DOMPasteAccess.h>
#include <WebCore/DocumentFragment.h>
#include <WebCore/FocusController.h>
#include <WebCore/FrameLoader.h>
#include <WebCore/HTMLInputElement.h>
#include <WebCore/HTMLNames.h>
#include <WebCore/HTMLTextAreaElement.h>
#include <WebCore/KeyboardEvent.h>
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameView.h>
#include <WebCore/NotImplemented.h>
#include <WebCore/Page.h>
#include <WebCore/Range.h>
#include <WebCore/SerializedAttachmentData.h>
#include <WebCore/SpellChecker.h>
#include <WebCore/StyleProperties.h>
#include <WebCore/TextIterator.h>
#include <WebCore/UndoStep.h>
#include <WebCore/UserTypingGestureIndicator.h>
#include <WebCore/VisibleUnits.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/StringView.h>

namespace WebKit {
using namespace WebCore;
using namespace HTMLNames;

WTF_MAKE_TZONE_ALLOCATED_IMPL(WebEditorClient);

bool WebEditorClient::shouldDeleteRange(const std::optional<SimpleRange>& range)
{
    return m_page->injectedBundleEditorClient().shouldDeleteRange(*m_page, range);
}

bool WebEditorClient::smartInsertDeleteEnabled()
{
    return m_page->isSmartInsertDeleteEnabled();
}
 
bool WebEditorClient::isSelectTrailingWhitespaceEnabled() const
{
    return m_page->isSelectTrailingWhitespaceEnabled();
}

bool WebEditorClient::isContinuousSpellCheckingEnabled()
{
    return WebProcess::singleton().textCheckerState().contains(TextCheckerState::ContinuousSpellCheckingEnabled);
}

void WebEditorClient::toggleContinuousSpellChecking()
{
    notImplemented();
}

bool WebEditorClient::isGrammarCheckingEnabled()
{
    return WebProcess::singleton().textCheckerState().contains(TextCheckerState::GrammarCheckingEnabled);
}

void WebEditorClient::toggleGrammarChecking()
{
    notImplemented();
}

int WebEditorClient::spellCheckerDocumentTag()
{
    notImplemented();
    return 0;
}

bool WebEditorClient::shouldBeginEditing(const SimpleRange& range)
{
    return m_page->injectedBundleEditorClient().shouldBeginEditing(*m_page, range);
}

bool WebEditorClient::shouldEndEditing(const SimpleRange& range)
{
    return m_page->injectedBundleEditorClient().shouldEndEditing(*m_page, range);
}

bool WebEditorClient::shouldInsertNode(Node& node, const std::optional<SimpleRange>& rangeToReplace, EditorInsertAction action)
{
    return m_page->injectedBundleEditorClient().shouldInsertNode(*m_page, node, rangeToReplace, action);
}

bool WebEditorClient::shouldInsertText(const String& text, const std::optional<SimpleRange>& rangeToReplace, EditorInsertAction action)
{
    return m_page->injectedBundleEditorClient().shouldInsertText(*m_page, text, rangeToReplace, action);
}

bool WebEditorClient::shouldChangeSelectedRange(const std::optional<SimpleRange>& fromRange, const std::optional<SimpleRange>& toRange, Affinity affinity, bool stillSelecting)
{
    return m_page->injectedBundleEditorClient().shouldChangeSelectedRange(*m_page, fromRange, toRange, affinity, stillSelecting);
}
    
bool WebEditorClient::shouldApplyStyle(const StyleProperties& style, const std::optional<SimpleRange>& range)
{
    return m_page->injectedBundleEditorClient().shouldApplyStyle(*m_page, style, range);
}

#if ENABLE(ATTACHMENT_ELEMENT)

void WebEditorClient::registerAttachmentIdentifier(const String& identifier, const String& contentType, const String& preferredFileName, Ref<FragmentedSharedBuffer>&& data)
{
    m_page->send(Messages::WebPageProxy::RegisterAttachmentIdentifierFromData(identifier, contentType, preferredFileName, IPC::SharedBufferReference(WTFMove(data))));
}

void WebEditorClient::registerAttachments(Vector<WebCore::SerializedAttachmentData>&& data)
{
    m_page->send(Messages::WebPageProxy::RegisterAttachmentsFromSerializedData(WTFMove(data)));
}

void WebEditorClient::registerAttachmentIdentifier(const String& identifier, const String& contentType, const String& filePath)
{
    m_page->send(Messages::WebPageProxy::RegisterAttachmentIdentifierFromFilePath(identifier, contentType, filePath));
}

void WebEditorClient::registerAttachmentIdentifier(const String& identifier)
{
    m_page->send(Messages::WebPageProxy::RegisterAttachmentIdentifier(identifier));
}

void WebEditorClient::cloneAttachmentData(const String& fromIdentifier, const String& toIdentifier)
{
    m_page->send(Messages::WebPageProxy::CloneAttachmentData(fromIdentifier, toIdentifier));
}

void WebEditorClient::didInsertAttachmentWithIdentifier(const String& identifier, const String& source, WebCore::AttachmentAssociatedElementType associatedElementType)
{
    m_page->send(Messages::WebPageProxy::DidInsertAttachmentWithIdentifier(identifier, source, associatedElementType));
}

void WebEditorClient::didRemoveAttachmentWithIdentifier(const String& identifier)
{
    m_page->send(Messages::WebPageProxy::DidRemoveAttachmentWithIdentifier(identifier));
}

Vector<SerializedAttachmentData> WebEditorClient::serializedAttachmentDataForIdentifiers(const Vector<String>& identifiers)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::SerializedAttachmentDataForIdentifiers(identifiers));
    auto [serializedData] = sendResult.takeReplyOr(Vector<WebCore::SerializedAttachmentData> { });
    return serializedData;
}

#endif

void WebEditorClient::didApplyStyle()
{
    m_page->didApplyStyle();
}

bool WebEditorClient::shouldMoveRangeAfterDelete(const SimpleRange&, const SimpleRange&)
{
    return true;
}

void WebEditorClient::didBeginEditing()
{
    // FIXME: What good is a notification name, if it's always the same?
    static NeverDestroyed<String> WebViewDidBeginEditingNotification(MAKE_STATIC_STRING_IMPL("WebViewDidBeginEditingNotification"));
    m_page->injectedBundleEditorClient().didBeginEditing(*m_page, WebViewDidBeginEditingNotification.get().impl());
}

void WebEditorClient::respondToChangedContents()
{
    static NeverDestroyed<String> WebViewDidChangeNotification(MAKE_STATIC_STRING_IMPL("WebViewDidChangeNotification"));
    m_page->injectedBundleEditorClient().didChange(*m_page, WebViewDidChangeNotification.get().impl());
    m_page->didChangeContents();
}

void WebEditorClient::respondToChangedSelection(LocalFrame* frame)
{
    static NeverDestroyed<String> WebViewDidChangeSelectionNotification(MAKE_STATIC_STRING_IMPL("WebViewDidChangeSelectionNotification"));
    m_page->injectedBundleEditorClient().didChangeSelection(*m_page, WebViewDidChangeSelectionNotification.get().impl());
    if (!frame)
        return;

    m_page->didChangeSelection(*frame);

#if PLATFORM(GTK)
    updateGlobalSelection(frame);
#endif
}

void WebEditorClient::didEndUserTriggeredSelectionChanges()
{
    m_page->didEndUserTriggeredSelectionChanges();
}

void WebEditorClient::updateEditorStateAfterLayoutIfEditabilityChanged()
{
    m_page->updateEditorStateAfterLayoutIfEditabilityChanged();
}

void WebEditorClient::didUpdateComposition()
{
    m_page->didUpdateComposition();
}

void WebEditorClient::discardedComposition(const Document& document)
{
    m_page->discardedComposition(document);
}

void WebEditorClient::canceledComposition()
{
    m_page->canceledComposition();
}

void WebEditorClient::didEndEditing()
{
    static NeverDestroyed<String> WebViewDidEndEditingNotification(MAKE_STATIC_STRING_IMPL("WebViewDidEndEditingNotification"));
    m_page->injectedBundleEditorClient().didEndEditing(*m_page, WebViewDidEndEditingNotification.get().impl());
}

void WebEditorClient::didWriteSelectionToPasteboard()
{
    m_page->injectedBundleEditorClient().didWriteToPasteboard(*m_page);
}

void WebEditorClient::willWriteSelectionToPasteboard(const std::optional<SimpleRange>& range)
{
    m_page->injectedBundleEditorClient().willWriteToPasteboard(*m_page, range);
}

void WebEditorClient::getClientPasteboardData(const std::optional<SimpleRange>& range, Vector<std::pair<String, RefPtr<WebCore::SharedBuffer>>>& pasteboardTypesAndData)
{
    Vector<String> pasteboardTypes;
    Vector<RefPtr<WebCore::SharedBuffer>> pasteboardData;
    for (size_t i = 0; i < pasteboardTypesAndData.size(); ++i) {
        pasteboardTypes.append(pasteboardTypesAndData[i].first);
        pasteboardData.append(pasteboardTypesAndData[i].second);
    }

    m_page->injectedBundleEditorClient().getPasteboardDataForRange(*m_page, range, pasteboardTypes, pasteboardData);

    ASSERT(pasteboardTypes.size() == pasteboardData.size());
    pasteboardTypesAndData.clear();
    for (size_t i = 0; i < pasteboardTypes.size(); ++i)
        pasteboardTypesAndData.append(std::make_pair(pasteboardTypes[i], pasteboardData[i]));
}

bool WebEditorClient::performTwoStepDrop(DocumentFragment& fragment, const SimpleRange& destination, bool isMove)
{
    return m_page->injectedBundleEditorClient().performTwoStepDrop(*m_page, fragment, destination, isMove);
}

void WebEditorClient::registerUndoStep(UndoStep& step)
{
    // FIXME: Add assertion that the command being reapplied is the same command that is
    // being passed to us.
    if (m_page->isInRedo())
        return;

    auto webStep = WebUndoStep::create(step);
    auto stepID = webStep->stepID();

    m_page->addWebUndoStep(stepID, WTFMove(webStep));
    m_page->send(Messages::WebPageProxy::RegisterEditCommandForUndo(stepID, step.label()), IPC::SendOption::DispatchMessageEvenWhenWaitingForSyncReply);
}

void WebEditorClient::registerRedoStep(UndoStep&)
{
}

void WebEditorClient::clearUndoRedoOperations()
{
    m_page->send(Messages::WebPageProxy::ClearAllEditCommands());
}

bool WebEditorClient::canCopyCut(LocalFrame*, bool defaultValue) const
{
    return defaultValue;
}

bool WebEditorClient::canPaste(LocalFrame*, bool defaultValue) const
{
    return defaultValue;
}

bool WebEditorClient::canUndo() const
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::CanUndoRedo(UndoOrRedo::Undo));
    auto [result] = sendResult.takeReplyOr(false);
    return result;
}

bool WebEditorClient::canRedo() const
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::CanUndoRedo(UndoOrRedo::Redo));
    auto [result] = sendResult.takeReplyOr(false);
    return result;
}

void WebEditorClient::undo()
{
    m_page->sendSync(Messages::WebPageProxy::ExecuteUndoRedo(UndoOrRedo::Undo));
}

void WebEditorClient::redo()
{
    m_page->sendSync(Messages::WebPageProxy::ExecuteUndoRedo(UndoOrRedo::Redo));
}

WebCore::DOMPasteAccessResponse WebEditorClient::requestDOMPasteAccess(WebCore::DOMPasteAccessCategory pasteAccessCategory, WebCore::FrameIdentifier frameID, const String& originIdentifier)
{
    return m_page->requestDOMPasteAccess(pasteAccessCategory, frameID, originIdentifier);
}

#if !PLATFORM(COCOA) && !USE(GLIB)

void WebEditorClient::handleKeyboardEvent(KeyboardEvent& event)
{
    if (m_page->handleEditingKeyboardEvent(event))
        event.setDefaultHandled();
}

void WebEditorClient::handleInputMethodKeydown(KeyboardEvent&)
{
    notImplemented();
}

#endif // !PLATFORM(COCOA) && !USE(GLIB)

void WebEditorClient::textFieldDidBeginEditing(Element& element)
{
    RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element);
    if (!inputElement)
        return;

    RefPtr frame = element.document().frame();
    RefPtr webFrame = WebFrame::fromCoreFrame(*frame);
    ASSERT(webFrame);

    m_page->injectedBundleFormClient().textFieldDidBeginEditing(m_page.get(), *inputElement, webFrame.get());
}

void WebEditorClient::textFieldDidEndEditing(Element& element)
{
    RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element);
    if (!inputElement)
        return;

    auto webFrame = WebFrame::fromCoreFrame(*element.document().frame());
    ASSERT(webFrame);

    m_page->injectedBundleFormClient().textFieldDidEndEditing(m_page.get(), *inputElement, webFrame.get());
}

void WebEditorClient::textDidChangeInTextField(Element& element)
{
    RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element);
    if (!inputElement)
        return;

    bool initiatedByUserTyping = UserTypingGestureIndicator::processingUserTypingGesture() && UserTypingGestureIndicator::focusedElementAtGestureStart() == inputElement;

    auto webFrame = WebFrame::fromCoreFrame(*element.document().frame());
    ASSERT(webFrame);

    m_page->injectedBundleFormClient().textDidChangeInTextField(m_page.get(), *inputElement, webFrame.get(), initiatedByUserTyping);
}

void WebEditorClient::textDidChangeInTextArea(Element& element)
{
    RefPtr textAreaElement = dynamicDowncast<HTMLTextAreaElement>(element);
    if (!textAreaElement)
        return;

    auto webFrame = WebFrame::fromCoreFrame(*element.document().frame());
    ASSERT(webFrame);

    m_page->injectedBundleFormClient().textDidChangeInTextArea(m_page.get(), *textAreaElement, webFrame.get());
}

#if !PLATFORM(IOS_FAMILY)

void WebEditorClient::overflowScrollPositionChanged()
{
}

void WebEditorClient::subFrameScrollPositionChanged()
{
}

#endif

static bool getActionTypeForKeyEvent(KeyboardEvent* event, WKInputFieldActionType& type)
{
    String key = event->keyIdentifier();
    if (key == "Up"_s)
        type = WKInputFieldActionTypeMoveUp;
    else if (key == "Down"_s)
        type = WKInputFieldActionTypeMoveDown;
    else if (key == "U+001B"_s)
        type = WKInputFieldActionTypeCancel;
    else if (key == "U+0009"_s) {
        if (event->shiftKey())
            type = WKInputFieldActionTypeInsertBacktab;
        else
            type = WKInputFieldActionTypeInsertTab;
    } else if (key == "Enter"_s)
        type = WKInputFieldActionTypeInsertNewline;
    else
        return false;

    return true;
}

static API::InjectedBundle::FormClient::InputFieldAction toInputFieldAction(WKInputFieldActionType action)
{
    switch (action) {
    case WKInputFieldActionTypeMoveUp:
        return API::InjectedBundle::FormClient::InputFieldAction::MoveUp;
    case WKInputFieldActionTypeMoveDown:
        return API::InjectedBundle::FormClient::InputFieldAction::MoveDown;
    case WKInputFieldActionTypeCancel:
        return API::InjectedBundle::FormClient::InputFieldAction::Cancel;
    case WKInputFieldActionTypeInsertTab:
        return API::InjectedBundle::FormClient::InputFieldAction::InsertTab;
    case WKInputFieldActionTypeInsertNewline:
        return API::InjectedBundle::FormClient::InputFieldAction::InsertNewline;
    case WKInputFieldActionTypeInsertDelete:
        return API::InjectedBundle::FormClient::InputFieldAction::InsertDelete;
    case WKInputFieldActionTypeInsertBacktab:
        return API::InjectedBundle::FormClient::InputFieldAction::InsertBacktab;
    }

    ASSERT_NOT_REACHED();
    return API::InjectedBundle::FormClient::InputFieldAction::Cancel;
}

bool WebEditorClient::doTextFieldCommandFromEvent(Element& element, KeyboardEvent* event)
{
    RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element);
    if (!inputElement)
        return false;

    WKInputFieldActionType actionType = static_cast<WKInputFieldActionType>(0);
    if (!getActionTypeForKeyEvent(event, actionType))
        return false;

    auto webFrame = WebFrame::fromCoreFrame(*element.document().frame());
    ASSERT(webFrame);

    return m_page->injectedBundleFormClient().shouldPerformActionInTextField(m_page.get(), *inputElement, toInputFieldAction(actionType), webFrame.get());
}

void WebEditorClient::textWillBeDeletedInTextField(Element& element)
{
    RefPtr inputElement = dynamicDowncast<HTMLInputElement>(element);
    if (!inputElement)
        return;

    auto webFrame = WebFrame::fromCoreFrame(*element.document().frame());
    ASSERT(webFrame);

    m_page->injectedBundleFormClient().shouldPerformActionInTextField(m_page.get(), *inputElement, toInputFieldAction(WKInputFieldActionTypeInsertDelete), webFrame.get());
}

bool WebEditorClient::shouldEraseMarkersAfterChangeSelection(WebCore::TextCheckingType type) const
{
    // This prevents erasing spelling and grammar markers to match AppKit.
#if PLATFORM(COCOA)
    return !(type == TextCheckingType::Spelling || type == TextCheckingType::Grammar);
#else
    UNUSED_PARAM(type);
    return true;
#endif
}

void WebEditorClient::ignoreWordInSpellDocument(const String& word)
{
    m_page->send(Messages::WebPageProxy::IgnoreWord(word));
}

void WebEditorClient::learnWord(const String& word)
{
    m_page->send(Messages::WebPageProxy::LearnWord(word));
}

void WebEditorClient::checkSpellingOfString(StringView text, int* misspellingLocation, int* misspellingLength)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::CheckSpellingOfString(text.toStringWithoutCopying()));
    auto [resultLocation, resultLength] = sendResult.takeReplyOr(-1, 0);
    *misspellingLocation = resultLocation;
    *misspellingLength = resultLength;
}

void WebEditorClient::checkGrammarOfString(StringView text, Vector<WebCore::GrammarDetail>& grammarDetails, int* badGrammarLocation, int* badGrammarLength)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::CheckGrammarOfString(text.toStringWithoutCopying()));
    int32_t resultLocation = -1;
    int32_t resultLength = 0;
    if (sendResult.succeeded())
        std::tie(grammarDetails, resultLocation, resultLength) = sendResult.takeReply();
    *badGrammarLocation = resultLocation;
    *badGrammarLength = resultLength;
}

static uint64_t insertionPointFromCurrentSelection(const VisibleSelection& currentSelection)
{
    auto selectionStart = currentSelection.visibleStart();
    auto range = makeSimpleRange(selectionStart, startOfParagraph(selectionStart));
    return range ? characterCount(*range) : 0;
}

#if USE(UNIFIED_TEXT_CHECKING)

Vector<TextCheckingResult> WebEditorClient::checkTextOfParagraph(StringView stringView, OptionSet<WebCore::TextCheckingType> checkingTypes, const VisibleSelection& currentSelection)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::CheckTextOfParagraph(stringView.toStringWithoutCopying(), checkingTypes, insertionPointFromCurrentSelection(currentSelection)));
    auto [results] = sendResult.takeReplyOr(Vector<TextCheckingResult> { });
    return results;
}

#endif

void WebEditorClient::updateSpellingUIWithGrammarString(const String& badGrammarPhrase, const GrammarDetail& grammarDetail)
{
    m_page->send(Messages::WebPageProxy::UpdateSpellingUIWithGrammarString(badGrammarPhrase, grammarDetail));
}

void WebEditorClient::updateSpellingUIWithMisspelledWord(const String& misspelledWord)
{
    m_page->send(Messages::WebPageProxy::UpdateSpellingUIWithMisspelledWord(misspelledWord));
}

void WebEditorClient::showSpellingUI(bool)
{
    notImplemented();
}

bool WebEditorClient::spellingUIIsShowing()
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::SpellingUIIsShowing());
    auto [isShowing] = sendResult.takeReplyOr(false);
    return isShowing;
}

void WebEditorClient::getGuessesForWord(const String& word, const String& context, const VisibleSelection& currentSelection, Vector<String>& guesses)
{
    auto sendResult = m_page->sendSync(Messages::WebPageProxy::GetGuessesForWord(word, context, insertionPointFromCurrentSelection(currentSelection)));
    if (sendResult.succeeded())
        std::tie(guesses) = sendResult.takeReply();
}

void WebEditorClient::requestCheckingOfString(TextCheckingRequest& request, const WebCore::VisibleSelection& currentSelection)
{
    auto requestID = TextCheckerRequestID::generate();
    m_page->addTextCheckingRequest(requestID, request);

    m_page->send(Messages::WebPageProxy::RequestCheckingOfString(requestID, request.data(), insertionPointFromCurrentSelection(currentSelection)));
}

void WebEditorClient::willChangeSelectionForAccessibility()
{
    m_page->willChangeSelectionForAccessibility();
}

void WebEditorClient::didChangeSelectionForAccessibility()
{
    m_page->didChangeSelectionForAccessibility();
}

void WebEditorClient::setInputMethodState(Element* element)
{
#if PLATFORM(GTK) || PLATFORM(WPE)
    m_page->setInputMethodState(element);
#else
    UNUSED_PARAM(element);
#endif
}

bool WebEditorClient::supportsGlobalSelection()
{
#if PLATFORM(GTK)
    return true;
#else
    return false;
#endif
}

} // namespace WebKit
