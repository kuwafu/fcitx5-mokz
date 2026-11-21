#include <fcitx/inputmethodengine.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addoninstance.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/event.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/key.h>
#include <exception>
#include <string>
#include <vector>
#include <set>
#include <algorithm>

#include "input/romaji_converter.hpp"
#include "input/kana_utils.hpp"
#include "nlp/viterbi.hpp"
#include "storage/history_manager.hpp"
#include "ui/candidate_builder.hpp"
#include "ui/preedit_renderer.hpp"

enum class ImeMode {
    INPUTTING,
    CONVERT_STAGE1,
    CONVERT_STAGE2,
    SEGMENT_NAV
};

class MinimalAddon : public fcitx::InputMethodEngineV2 {
public:
    explicit MinimalAddon(fcitx::AddonManager* manager)
        : manager_(manager) {
        if (!minimal_ime::ViterbiEngine::ensureEngines()) {
            FCITX_ERROR() << "[MinimalAddon] Failed to initialize hp-ime Stage1/Reranker engines";
        }
        history_.initFromHome();
    }

    ~MinimalAddon() override {
        history_.save();
    }

    std::vector<fcitx::InputMethodEntry> listInputMethods() override {
        std::vector<fcitx::InputMethodEntry> entries;
        entries.emplace_back("minimal-ime", "Minimal IME", "ja", "minimal_addon")
               .setLabel("あ")
               .setIcon("input-keyboard");
        return entries;
    }

    void activate(const fcitx::InputMethodEntry&, fcitx::InputContextEvent&) override {}
    void deactivate(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& event) override {
        commitPendingOnDeactivate(event.inputContext());
        resetState(event.inputContext());
    }
    void reset(const fcitx::InputMethodEntry&, fcitx::InputContextEvent& event) override {
        resetState(event.inputContext());
    }

private:
    std::string makeDisplayedTextForCommit() {
        std::string display;
        const bool hasSelectedCandidate =
            !currentSegmentCandidates_.empty() && selectedIndex_ >= 0 &&
            selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size());
        const std::string chosen =
            hasSelectedCandidate ? currentSegmentCandidates_[selectedIndex_] : std::string();

        if (currentMode_ == ImeMode::INPUTTING) {
            display = currentPreedit_ + romajiConverter_.pending();
        } else if (currentMode_ == ImeMode::CONVERT_STAGE1) {
            display = (hasSelectedCandidate && !chosen.empty())
                          ? chosen
                          : currentPreedit_ + romajiConverter_.pending();
        } else {
            if (!currentSegments_.empty()) {
                if (hasSelectedCandidate && !chosen.empty() && isCurrentSelectionWhole()) {
                    display = chosen;
                } else {
                    for (size_t i = 0; i < currentSegments_.size(); ++i) {
                        const std::string& w =
                            (static_cast<int>(i) == focusedSegmentIndex_ && hasSelectedCandidate && !chosen.empty())
                                ? chosen
                                : currentSegments_[i].word;
                        display += w;
                    }
                }
            } else if (hasSelectedCandidate && !chosen.empty()) {
                display = chosen;
            } else {
                display = currentPreedit_ + romajiConverter_.pending();
            }
        }

        return display;
    }

    void commitPendingOnDeactivate(fcitx::InputContext* ic) {
        if (!ic) return;

        const std::string display = makeDisplayedTextForCommit();
        if (!display.empty()) {
            ic->commitString(display);
        }
    }

    minimal_ime::SuggestWord::SelectCallback makeSelectCallback() {
        return [this](fcitx::InputContext* ic, int index) {
            this->selectCandidateFromMouse(ic, index);
        };
    }

    void renderCandidatePopup(fcitx::InputContext* ic) {
        minimal_ime::PreeditRenderer::renderCandidatePopup(
            ic, currentSegmentCandidates_, selectedIndex_, makeSelectCallback());
    }

    void updateInputtingSuggestions(fcitx::InputContext* ic) {
        if (!ic) return;
        std::string evalYomi = currentPreedit_;
        if (romajiConverter_.pending() == "n") evalYomi += "ん";

        minimal_ime::CandidateBuilder::buildInputtingSuggestions(
            evalYomi, rawInput_, history_, forcedBoundaries_,
            currentSegmentCandidates_, currentCandidateIsWhole_);

        selectedIndex_ = 0;
        renderCandidatePopup(ic);
    }

    void renderInputtingPreedit(fcitx::InputContext* ic) {
        if (!ic) return;
        std::string display = currentPreedit_ + romajiConverter_.pending();
        if (display.empty()) {
            currentSegmentCandidates_.clear();
            currentCandidateIsWhole_.clear();
            ic->inputPanel().reset();
            ic->updatePreedit();
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
            return;
        }
        fcitx::Text text(display, fcitx::TextFormatFlag::Underline);
        minimal_ime::PreeditRenderer::setClientOrServerPreedit(ic, text);
        updateInputtingSuggestions(ic);
    }

    void renderStage1Preedit(fcitx::InputContext* ic) {
        minimal_ime::PreeditRenderer::renderStage1Preedit(
            ic, currentSegmentCandidates_, selectedIndex_, updatingPreview_);
    }

    bool isCurrentSelectionWhole() const {
        if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(currentCandidateIsWhole_.size())) {
            return currentCandidateIsWhole_[selectedIndex_];
        }
        return false;
    }

    void renderSegmentsPreedit(fcitx::InputContext* ic) {
        std::string chosen;
        if (currentMode_ == ImeMode::CONVERT_STAGE2 && !currentSegmentCandidates_.empty() &&
            selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size())) {
            chosen = currentSegmentCandidates_[selectedIndex_];
        }
        minimal_ime::PreeditRenderer::renderSegmentsPreedit(
            ic, currentSegments_, focusedSegmentIndex_, chosen, isCurrentSelectionWhole(), updatingPreview_);
    }

    
    void syncForcedBoundaries() {
        forcedBoundaries_.clear();
        if (currentSegments_.empty()) return;

        const int totalChars = static_cast<int>(minimal_ime::KanaUtils::countUtf8Chars(currentPreedit_));
        if (totalChars <= 0) return;

        for (const auto& seg : currentSegments_) {
            const int start = std::clamp(seg.startChar, 0, totalChars);
            const int end = std::clamp(seg.endChar, start, totalChars);
            if (start > 0 && start < totalChars) forcedBoundaries_.insert(start);
            if (end > 0 && end < totalChars) forcedBoundaries_.insert(end);
        }
    }

    void resegmentFromFocused(fcitx::InputContext* ic, int chosenCharLen, const std::string& chosenWord) {
        if (focusedSegmentIndex_ < 0 || focusedSegmentIndex_ >= static_cast<int>(currentSegments_.size())) return;

        const int totalChars = static_cast<int>(minimal_ime::KanaUtils::countUtf8Chars(currentPreedit_));
        if (totalChars <= 0) return;

        auto& activeSeg = currentSegments_[focusedSegmentIndex_];
        const int segStart = std::clamp(activeSeg.startChar, 0, totalChars);
        if (segStart >= totalChars) {
            activeSeg.startChar = activeSeg.endChar = totalChars;
            if (static_cast<int>(currentSegments_.size()) > focusedSegmentIndex_ + 1) {
                currentSegments_.resize(focusedSegmentIndex_ + 1);
            }
            syncForcedBoundaries();
            return;
        }

        if (chosenCharLen <= 0) {
            activeSeg.startChar = segStart;
            activeSeg.endChar = std::clamp(activeSeg.endChar, segStart, totalChars);
            syncForcedBoundaries();
            return;
        }

        const int maxLen = totalChars - segStart;
        const int safeLen = std::min(chosenCharLen, maxLen);
        const int newEnd = segStart + safeLen;

        activeSeg.startChar = segStart;
        activeSeg.endChar = newEnd;
        activeSeg.word = chosenWord;

        if (newEnd >= totalChars) {
            if (static_cast<int>(currentSegments_.size()) > focusedSegmentIndex_ + 1) {
                currentSegments_.resize(focusedSegmentIndex_ + 1);
            }
            syncForcedBoundaries();
            return;
        }

        std::set<int> boundaries;
        for (int i = 0; i <= focusedSegmentIndex_; ++i) {
            const int start = std::clamp(currentSegments_[i].startChar, 0, totalChars);
            const int end = std::clamp(currentSegments_[i].endChar, start, totalChars);
            if (start > 0 && start < totalChars) boundaries.insert(start);
            if (i < focusedSegmentIndex_ && end > 0 && end < totalChars) boundaries.insert(end);
        }
        boundaries.insert(newEnd);

        auto reseg = minimal_ime::ViterbiEngine::convertToSegments(
            currentPreedit_, history_.commitHistory(), history_.bigramHistory(),
            boundaries, activeSeg.word);

        // focusedSegmentIndex_ までの確定部分を維持し、後ろだけ差し替え
        std::vector<minimal_ime::ViterbiSegment> updated;
        updated.reserve(currentSegments_.size());
        for (int i = 0; i <= focusedSegmentIndex_; ++i) {
            updated.push_back(currentSegments_[i]);
        }
        for (const auto& seg : reseg) {
            if (seg.startChar >= newEnd) {
                updated.push_back(seg);
            }
        }
        currentSegments_ = std::move(updated);
        syncForcedBoundaries();
    }

    void showCandidateList(fcitx::InputContext* ic) {
        if (!ic || currentSegments_.empty()) return;
        minimal_ime::CandidateBuilder::buildConversionCandidatesDynamic(
            currentPreedit_, rawInput_, currentSegments_, focusedSegmentIndex_,
            segmentEdited_, history_, currentSegmentCandidates_, candidateCharLengths_, currentCandidateIsWhole_);
        renderCandidatePopup(ic);
    }

    void cancelConversionToInputting(fcitx::InputContext* ic) {
        currentSegments_.clear();
        currentSegmentCandidates_.clear();
        currentCandidateIsWhole_.clear();
        candidateCharLengths_.clear();
        forcedBoundaries_.clear();
        focusedSegmentIndex_ = 0;
        selectedIndex_ = 0;
        segmentEdited_ = false;
        currentMode_ = ImeMode::INPUTTING;

        if (ic) {
            ic->inputPanel().reset();
            renderInputtingPreedit(ic);
            updateInputtingSuggestions(ic);
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
        }
    }

    void resetState(fcitx::InputContext* ic) {
        currentPreedit_.clear();
        rawInput_.clear();
        romajiConverter_.clear();
        currentSegments_.clear();
        currentSegmentCandidates_.clear();
        currentCandidateIsWhole_.clear();
        forcedBoundaries_.clear();
        focusedSegmentIndex_ = 0;
        selectedIndex_ = 0;
        segmentEdited_ = false;
        currentMode_ = ImeMode::INPUTTING;
        if (ic) {
            ic->inputPanel().reset();
            ic->updatePreedit();
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
        }
    }

    void insertShiftUppercase(fcitx::InputContext* ic, char c) {
        auto res = romajiConverter_.appendLiteral(c);
        if (!res.committedKana.empty()) currentPreedit_ += res.committedKana;
        rawInput_ += c;
        currentMode_ = ImeMode::INPUTTING;
        renderInputtingPreedit(ic);
    }

    void backspaceInConversion(fcitx::InputContext* ic) {
        // 変換中の BackSpace は MS-IME 準拠: 現在表示されている変換済みテキストの末尾を 1 コードポイント削除する。
        std::string displayed;
        const bool hasSelectedCandidate =
            !currentSegmentCandidates_.empty() && selectedIndex_ >= 0 &&
            selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size());
        const std::string chosen =
            hasSelectedCandidate ? currentSegmentCandidates_[selectedIndex_] : std::string();

        if (!currentSegments_.empty()) {
            if (hasSelectedCandidate && !chosen.empty() && isCurrentSelectionWhole()) {
                displayed = chosen;
            } else {
                for (size_t i = 0; i < currentSegments_.size(); ++i) {
                    const std::string& w =
                        (static_cast<int>(i) == focusedSegmentIndex_ && hasSelectedCandidate && !chosen.empty())
                            ? chosen
                            : currentSegments_[i].word;
                    displayed += w;
                }
            }
        } else if (hasSelectedCandidate && !chosen.empty()) {
            displayed = chosen;
        } else {
            displayed = currentPreedit_;
        }

        if (displayed.empty()) {
            resetState(ic);
            return;
        }

        minimal_ime::KanaUtils::popBackUtf8(displayed);
        if (displayed.empty()) {
            resetState(ic);
            return;
        }

        currentPreedit_ = displayed;
        rawInput_.clear();
        romajiConverter_.clear();
        currentSegments_.clear();
        currentSegmentCandidates_.clear();
        currentCandidateIsWhole_.clear();
        candidateCharLengths_.clear();
        forcedBoundaries_.clear();
        focusedSegmentIndex_ = 0;
        selectedIndex_ = 0;
        segmentEdited_ = false;
        currentMode_ = ImeMode::INPUTTING;

        if (ic) {
            // 候補ウィンドウを閉じないため inputPanel().reset() は呼ばない。
            // 残った未変換文字列に対するサジェストを再構築して表示する。
            renderInputtingPreedit(ic);
            if (currentSegmentCandidates_.empty() && !currentPreedit_.empty()) {
                currentSegmentCandidates_.push_back(currentPreedit_);
                currentCandidateIsWhole_.clear();
                currentCandidateIsWhole_.push_back(true);
                selectedIndex_ = 0;
                renderCandidatePopup(ic);
            }
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
        }
    }

    void startViterbiConversion(fcitx::InputContext* ic) {
        currentPreedit_ += romajiConverter_.flush();
        currentSegments_ = minimal_ime::ViterbiEngine::convertToSegments(
            currentPreedit_, history_.commitHistory(), history_.bigramHistory(),
            forcedBoundaries_, history_.lastCommittedWord());
        if (!currentSegments_.empty()) {
            focusedSegmentIndex_ = 0;
            selectedIndex_ = 0;
            segmentEdited_ = false;
            currentMode_ = ImeMode::CONVERT_STAGE2;
            showCandidateList(ic);
            renderSegmentsPreedit(ic);
        }
    }

    void commitStage1Selection(fcitx::InputContext* ic) {
        if (!ic) return;
        std::string yomi = currentPreedit_ + romajiConverter_.flush();
        currentPreedit_ = yomi;
        std::string chosen = (!currentSegmentCandidates_.empty() && selectedIndex_ >= 0 &&
                              selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size()))
                                 ? currentSegmentCandidates_[selectedIndex_]
                                 : yomi;
        ic->commitString(chosen);
        history_.recordCommit(yomi, chosen);
        resetState(ic);
    }

    void commitCurrentSelection(fcitx::InputContext* ic) {
        if (!ic) return;
        if (currentMode_ == ImeMode::CONVERT_STAGE1) {
            commitStage1Selection(ic);
            return;
        }
        if (currentSegments_.empty()) return;

        currentPreedit_ += romajiConverter_.flush();
        std::string chosen = (!currentSegmentCandidates_.empty() && selectedIndex_ >= 0 &&
                              selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size()))
                                 ? currentSegmentCandidates_[selectedIndex_]
                                 : currentSegments_[focusedSegmentIndex_].word;

        auto learnSegment = [&](const minimal_ime::ViterbiSegment& seg) {
            if (!seg.stemYomi.empty() && !seg.suffixWord.empty() && seg.word.size() > seg.suffixWord.size()) {
                std::string stemWord = seg.word.substr(0, seg.word.size() - seg.suffixWord.size());
                history_.recordCommit(seg.stemYomi, stemWord);
            } else {
                history_.recordCommit(seg.yomi, seg.word);
            }
        };

        if (isCurrentSelectionWhole()) {
            ic->commitString(chosen);
            if (currentSegments_.size() == 1) {
                history_.recordCommit(currentPreedit_, chosen);
            } else {
                for (const auto& seg : currentSegments_) learnSegment(seg);
            }
            resetState(ic);
            return;
        }

        currentSegments_[focusedSegmentIndex_].word = chosen;
        std::string commitStr;
        for (const auto& seg : currentSegments_) {
            commitStr += seg.word;
            learnSegment(seg);
        }
        ic->commitString(commitStr);
        resetState(ic);
    }

    void selectCandidateFromMouse(fcitx::InputContext* ic, int index) {
        if (!ic || index < 0 || index >= static_cast<int>(currentSegmentCandidates_.size())) return;
        selectedIndex_ = index;
        if (currentMode_ == ImeMode::INPUTTING || currentMode_ == ImeMode::CONVERT_STAGE1) {
            commitStage1Selection(ic);
            return;
        }
        commitCurrentSelection(ic);
    }

public:
    void keyEvent(const fcitx::InputMethodEntry&, fcitx::KeyEvent& event) override try {
        if (event.isRelease()) return;
        auto* ic = event.inputContext();
        if (!ic) return;

        // 原因: event.key() は正規化キーで、Shift+A でも Shift 状態が失われる場合がある。
        // その結果 isShiftUppercaseLetter が false になり、汎用 printable 分岐で
        // romajiConverter_.feed('A') に渡り 'a' に正規化されて あ になっていた。
        // 修正: rawKey を参照し、Shift 英字をローマ字変換前に必ず拾う。
        const auto rawKey = event.rawKey();
        const auto sym = rawKey.sym();
        const bool isSpace = (sym == FcitxKey_space);
        const bool isEnter = (sym == FcitxKey_Return || sym == FcitxKey_KP_Enter);
        const bool isTab = (sym == FcitxKey_Tab || sym == FcitxKey_ISO_Left_Tab);
        const bool isEscape = (sym == FcitxKey_Escape);
        const bool isBackSpace = (sym == FcitxKey_BackSpace);
        const bool isLeft = (sym == FcitxKey_Left);
        const bool isRight = (sym == FcitxKey_Right);
        const bool isFnKey = (sym >= FcitxKey_F6 && sym <= FcitxKey_F10);



        const auto states = static_cast<unsigned long long>(rawKey.states());
        const bool hasCtrl = (states & static_cast<unsigned long long>(fcitx::KeyState::Ctrl)) != 0;
        const bool hasAlt = (states & static_cast<unsigned long long>(fcitx::KeyState::Alt)) != 0;
        const bool hasShift = (states & static_cast<unsigned long long>(fcitx::KeyState::Shift)) != 0;

        const bool isShiftUppercaseLetter =
            !hasCtrl && !hasAlt &&
            ((sym >= 'A' && sym <= 'Z') ||
             (hasShift && sym >= 'a' && sym <= 'z'));
        char shiftUppercaseChar = 0;
        if (isShiftUppercaseLetter) {
            shiftUppercaseChar = static_cast<char>(
                (sym >= 'a' && sym <= 'z') ? sym - 'a' + 'A' : sym);
        }

        const bool isPageUp = (sym == FcitxKey_Page_Up || sym == FcitxKey_KP_Page_Up);
        const bool isPageDown = (sym == FcitxKey_Page_Down || sym == FcitxKey_KP_Page_Down);

        int digitSelectIndex = -1;
        if (!hasCtrl && !hasAlt && !hasShift) {
            if (sym >= FcitxKey_1 && sym <= FcitxKey_9) {
                digitSelectIndex = static_cast<int>(sym - FcitxKey_1);
            } else if (sym >= FcitxKey_KP_1 && sym <= FcitxKey_KP_9) {
                digitSelectIndex = static_cast<int>(sym - FcitxKey_KP_1);
            }
        }

        const bool isPurgeKey = (sym == FcitxKey_Delete || sym == FcitxKey_KP_Delete ||
                                 (hasShift && isBackSpace) ||
                                 (hasCtrl && (sym == FcitxKey_d || sym == FcitxKey_D)));

        // --- F6 / F7 / F8 / F9 / F10: 選択区画のみ変換し未確定のまま維持 ---
        if (isFnKey && (!currentPreedit_.empty() || !romajiConverter_.pending().empty())) {
            currentPreedit_ += romajiConverter_.flush();

            if (currentMode_ == ImeMode::INPUTTING) {
                std::string converted = minimal_ime::KanaUtils::applyFnConversion(currentPreedit_, rawInput_, sym);
                const int totalChars = static_cast<int>(minimal_ime::KanaUtils::countUtf8Chars(currentPreedit_));

                currentSegments_.clear();
                currentSegments_.push_back({0, totalChars, currentPreedit_, converted, converted, ""});
                focusedSegmentIndex_ = 0;

                currentSegmentCandidates_.clear();
                currentSegmentCandidates_.push_back(converted);
                candidateCharLengths_.clear();
                candidateCharLengths_.push_back(totalChars);
                currentCandidateIsWhole_.clear();
                currentCandidateIsWhole_.push_back(true);
                selectedIndex_ = 0;
                segmentEdited_ = true;
                currentMode_ = ImeMode::CONVERT_STAGE2;

                if (ic) {
                    ic->inputPanel().setCandidateList(nullptr);
                    renderSegmentsPreedit(ic);
                    ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
                }
            } else {
                if (!currentSegments_.empty()) {
                    focusedSegmentIndex_ = std::clamp(focusedSegmentIndex_, 0, static_cast<int>(currentSegments_.size()) - 1);
                    auto& seg = currentSegments_[focusedSegmentIndex_];
                    std::string rawForSeg = (currentSegments_.size() == 1 && !rawInput_.empty())
                                                ? rawInput_
                                                : minimal_ime::KanaUtils::kanaToRomaji(seg.yomi);
                    std::string converted = minimal_ime::KanaUtils::applyFnConversion(seg.yomi, rawForSeg, sym);
                    seg.word = converted;
                    segmentEdited_ = true;

                    auto it = std::find(currentSegmentCandidates_.begin(), currentSegmentCandidates_.end(), converted);
                    if (it != currentSegmentCandidates_.end()) {
                        currentSegmentCandidates_.erase(it);
                    }
                    currentSegmentCandidates_.insert(currentSegmentCandidates_.begin(), converted);
                    selectedIndex_ = 0;

                    if (ic) {
                        renderCandidatePopup(ic);
                        renderSegmentsPreedit(ic);
                    }
                }
            }
            event.filterAndAccept();
            return;
        }

        // --- 状態 1: INPUTTING ---
        if (currentMode_ == ImeMode::INPUTTING) {
            if (isPurgeKey && !currentSegmentCandidates_.empty()) {
                history_.purgeWord(currentSegmentCandidates_[0]);
                updateInputtingSuggestions(ic);
                event.filterAndAccept();
                return;
            }

            if (isBackSpace && !hasCtrl && !hasAlt) {
                if (!romajiConverter_.pending().empty() || !currentPreedit_.empty()) {
                    if (!rawInput_.empty()) rawInput_.pop_back();
                    if (!romajiConverter_.backspace()) minimal_ime::KanaUtils::popBackUtf8(currentPreedit_);
                    renderInputtingPreedit(ic);
                    event.filterAndAccept();
                    return;
                }
                return;
            }

            if (isTab && (!currentPreedit_.empty() || !romajiConverter_.pending().empty())) {
                currentPreedit_ += romajiConverter_.flush();
                if (currentSegmentCandidates_.empty()) updateInputtingSuggestions(ic);
                if (!currentSegmentCandidates_.empty()) {
                    currentMode_ = ImeMode::CONVERT_STAGE1;
                    selectedIndex_ = 0;
                    renderCandidatePopup(ic);
                    renderStage1Preedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (isSpace && (!currentPreedit_.empty() || !romajiConverter_.pending().empty())) {
                startViterbiConversion(ic);
                event.filterAndAccept();
                return;
            }

            if (isEnter && (!currentPreedit_.empty() || !romajiConverter_.pending().empty())) {
                currentPreedit_ += romajiConverter_.flush();
                std::string commitStr = currentPreedit_;
                ic->commitString(commitStr);
                history_.recordCommit(commitStr, commitStr);
                resetState(ic);
                event.filterAndAccept();
                return;
            }

            if (isEscape) {
                if (!currentPreedit_.empty() || !romajiConverter_.pending().empty()) {
                    resetState(ic);
                    event.filterAndAccept();
                    return;
                }
            }

            if (isShiftUppercaseLetter) {
                insertShiftUppercase(ic, shiftUppercaseChar);
                event.filterAndAccept();
                return;
            }

            if (!hasCtrl && !hasAlt && sym >= 0x20 && sym <= 0x7e) {
                char c = static_cast<char>(sym);
                rawInput_ += c;
                auto res = romajiConverter_.feed(c);
                if (!res.committedKana.empty()) currentPreedit_ += res.committedKana;
                renderInputtingPreedit(ic);
                event.filterAndAccept();
                return;
            }
            return;
        }

        // --- 状態 2: CONVERT_STAGE1 (Tab サジェスト選択) ---
        if (currentMode_ == ImeMode::CONVERT_STAGE1) {
            if (isPurgeKey && !currentSegmentCandidates_.empty()) {
                int delIdx = std::clamp(selectedIndex_, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                history_.purgeWord(currentSegmentCandidates_[delIdx]);
                currentSegmentCandidates_.erase(currentSegmentCandidates_.begin() + delIdx);
                if (currentSegmentCandidates_.empty()) {
                    currentMode_ = ImeMode::INPUTTING;
                    renderInputtingPreedit(ic);
                } else {
                    selectedIndex_ = std::clamp(delIdx, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                    renderCandidatePopup(ic);
                    renderStage1Preedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (digitSelectIndex >= 0 && !currentSegmentCandidates_.empty()) {
                int pageOffset = (selectedIndex_ / minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE) * minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE;
                int targetIdx = pageOffset + digitSelectIndex;
                if (targetIdx < static_cast<int>(currentSegmentCandidates_.size())) {
                    selectedIndex_ = targetIdx;
                    commitStage1Selection(ic);
                    event.filterAndAccept();
                    return;
                }
            }

            if ((isPageDown || isPageUp) && !currentSegmentCandidates_.empty()) {
                int delta = isPageDown ? minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE
                                       : -minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE;
                selectedIndex_ = std::clamp(selectedIndex_ + delta, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                renderCandidatePopup(ic);
                renderStage1Preedit(ic);
                event.filterAndAccept();
                return;
            }

            if (isBackSpace && !hasShift) {
                backspaceInConversion(ic);
                event.filterAndAccept();
                return;
            }

            if (isSpace) {
                startViterbiConversion(ic);
                event.filterAndAccept();
                return;
            }

            if ((isTab && !hasShift) || sym == FcitxKey_Down) {
                if (!currentSegmentCandidates_.empty()) {
                    selectedIndex_ = (selectedIndex_ + 1) % currentSegmentCandidates_.size();
                    renderCandidatePopup(ic);
                    renderStage1Preedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if ((isTab && hasShift) || sym == FcitxKey_Up) {
                if (!currentSegmentCandidates_.empty()) {
                    selectedIndex_ = (selectedIndex_ - 1 + currentSegmentCandidates_.size()) % currentSegmentCandidates_.size();
                    renderCandidatePopup(ic);
                    renderStage1Preedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (isEnter || isRight) {
                commitStage1Selection(ic);
                event.filterAndAccept();
                return;
            }

            if (isEscape) {
                cancelConversionToInputting(ic);
                event.filterAndAccept();
                return;
            }

            if (isShiftUppercaseLetter) {
                commitStage1Selection(ic);
                insertShiftUppercase(ic, shiftUppercaseChar);
                event.filterAndAccept();
                return;
            }

            if (!hasCtrl && !hasAlt && sym >= 0x20 && sym <= 0x7e) {
                commitStage1Selection(ic);
                char c = static_cast<char>(sym);
                rawInput_ += c;
                auto res = romajiConverter_.feed(c);
                if (!res.committedKana.empty()) currentPreedit_ += res.committedKana;
                renderInputtingPreedit(ic);
                event.filterAndAccept();
                return;
            }
            return;
        }

        // --- 状態 3: CONVERT_STAGE2 (Viterbi 連文節変換) ---
        if (currentMode_ == ImeMode::CONVERT_STAGE2) {
            if (isPurgeKey && !currentSegmentCandidates_.empty()) {
                int delIdx = std::clamp(selectedIndex_, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                history_.purgeWord(currentSegmentCandidates_[delIdx]);

                currentSegmentCandidates_.erase(currentSegmentCandidates_.begin() + delIdx);
                if (delIdx < static_cast<int>(currentCandidateIsWhole_.size())) {
                    currentCandidateIsWhole_.erase(currentCandidateIsWhole_.begin() + delIdx);
                }

                if (currentSegmentCandidates_.empty()) {
                    selectedIndex_ = 0;
                } else {
                    selectedIndex_ = std::clamp(delIdx, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                    if (!isCurrentSelectionWhole() && selectedIndex_ < static_cast<int>(candidateCharLengths_.size())) {
                        segmentEdited_ = true;
                        resegmentFromFocused(ic, candidateCharLengths_[selectedIndex_], currentSegmentCandidates_[selectedIndex_]);
                    }
                }
                renderCandidatePopup(ic);
                renderSegmentsPreedit(ic);
                event.filterAndAccept();
                return;
            }

            if (digitSelectIndex >= 0 && !currentSegmentCandidates_.empty()) {
                int pageOffset = (selectedIndex_ / minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE) * minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE;
                int targetIdx = pageOffset + digitSelectIndex;
                if (targetIdx < static_cast<int>(currentSegmentCandidates_.size())) {
                    selectedIndex_ = targetIdx;
                    commitCurrentSelection(ic);
                    event.filterAndAccept();
                    return;
                }
            }

            if ((isPageDown || isPageUp) && !currentSegmentCandidates_.empty()) {
                int delta = isPageDown ? minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE
                                       : -minimal_ime::PreeditRenderer::CANDIDATE_PAGE_SIZE;
                selectedIndex_ = std::clamp(selectedIndex_ + delta, 0, static_cast<int>(currentSegmentCandidates_.size()) - 1);
                if (segmentEdited_ && !isCurrentSelectionWhole()) {
                    currentSegments_[focusedSegmentIndex_].word = currentSegmentCandidates_[selectedIndex_];
                }
                renderCandidatePopup(ic);
                renderSegmentsPreedit(ic);
                event.filterAndAccept();
                return;
            }

            if (isBackSpace && !hasShift) {
                backspaceInConversion(ic);
                event.filterAndAccept();
                return;
            }

            // Shift + 左右: フォーカス文節を物理伸縮し、後続のみ局所再構築
            if ((isLeft || isRight) && hasShift && !currentSegments_.empty()) {
                segmentEdited_ = true;
                focusedSegmentIndex_ = std::clamp(focusedSegmentIndex_, 0, static_cast<int>(currentSegments_.size()) - 1);
                auto& seg = currentSegments_[focusedSegmentIndex_];
                const int totalChars = static_cast<int>(minimal_ime::KanaUtils::countUtf8Chars(currentPreedit_));
                const int newEnd = seg.endChar + (isRight ? 1 : -1);

                // 最低1文字以上、かつ全体の文字数を超えない範囲
                if (newEnd > seg.startChar && newEnd <= totalChars) {
                    // UTF-8 文字インデックスから部分文字列を安全に切り出すラムダ
                    auto getSubstrByChar = [](const std::string& str, int startChar, int charCount) -> std::string {
                        size_t byteStart = 0, byteEnd = str.size();
                        int curChar = 0;
                        for (size_t i = 0; i < str.size(); ) {
                            if (curChar == startChar) byteStart = i;
                            if (curChar == startChar + charCount) {
                                byteEnd = i;
                                break;
                            }
                            unsigned char c = static_cast<unsigned char>(str[i]);
                            if ((c & 0x80) == 0) i += 1;
                            else if ((c & 0xE0) == 0xC0) i += 2;
                            else if ((c & 0xF0) == 0xE0) i += 3;
                            else if ((c & 0xF8) == 0xF0) i += 4;
                            else i += 1;
                            curChar++;
                        }
                        if (curChar < startChar + charCount) byteEnd = str.size();
                        return str.substr(byteStart, byteEnd - byteStart);
                    };

                    seg.endChar = newEnd;
                    seg.yomi = getSubstrByChar(currentPreedit_, seg.startChar, newEnd - seg.startChar);
                    seg.stemYomi = seg.yomi;
                    seg.suffixWord.clear();
                    seg.word = seg.yomi;

                    // 後続テキストが存在する場合は局所再構築、なければ後続を切り落とす
                    currentSegments_.resize(focusedSegmentIndex_ + 1);
                    if (newEnd < totalChars) {
                        const std::string restYomi = getSubstrByChar(currentPreedit_, newEnd, totalChars - newEnd);
                        auto restSegments = minimal_ime::ViterbiEngine::convertToSegments(
                            restYomi, history_.commitHistory(), history_.bigramHistory(),
                            std::set<int>{}, seg.word);

                        if (restSegments.empty()) {
                            currentSegments_.push_back({newEnd, totalChars, restYomi, restYomi, restYomi, ""});
                        } else {
                            for (auto& rseg : restSegments) {
                                rseg.startChar += newEnd;
                                rseg.endChar += newEnd;
                                currentSegments_.push_back(rseg);
                            }
                        }
                    }

                    syncForcedBoundaries();
                    selectedIndex_ = 0;
                    showCandidateList(ic);

                    if (!currentSegmentCandidates_.empty()) {
                        currentSegments_[focusedSegmentIndex_].word = currentSegmentCandidates_[0];
                    }

                    renderCandidatePopup(ic);
                    renderSegmentsPreedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            // 左右: No-Enter Navigation
            if ((isLeft || isRight) && !hasShift && !currentSegments_.empty()) {
                if (selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(currentSegmentCandidates_.size())) {
                    if (!isCurrentSelectionWhole()) {
                        currentSegments_[focusedSegmentIndex_].word = currentSegmentCandidates_[selectedIndex_];
                    } else if (selectedIndex_ > 0) {
                        commitCurrentSelection(ic);
                        event.filterAndAccept();
                        return;
                    }
                }

                int nSeg = static_cast<int>(currentSegments_.size());
                if (isRight) {
                    if (focusedSegmentIndex_ + 1 >= nSeg) {
                        segmentEdited_ = true;
                        commitCurrentSelection(ic);
                        event.filterAndAccept();
                        return;
                    }
                    segmentEdited_ = true;
                    focusedSegmentIndex_++;
                } else {
                    segmentEdited_ = true;
                    focusedSegmentIndex_ = (focusedSegmentIndex_ - 1 + nSeg) % nSeg;
                }

                selectedIndex_ = 0;
                showCandidateList(ic);
                renderSegmentsPreedit(ic);
                event.filterAndAccept();
                return;
            }

            if (isTab || isSpace || sym == FcitxKey_Down) {
                if (!currentSegmentCandidates_.empty()) {
                    selectedIndex_ = (selectedIndex_ + 1) % currentSegmentCandidates_.size();
                    if (!isCurrentSelectionWhole() && selectedIndex_ < static_cast<int>(candidateCharLengths_.size())) {
                        segmentEdited_ = true;
                        resegmentFromFocused(ic, candidateCharLengths_[selectedIndex_], currentSegmentCandidates_[selectedIndex_]);
                    }
                    renderCandidatePopup(ic);
                    renderSegmentsPreedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (sym == FcitxKey_Up) {
                if (!currentSegmentCandidates_.empty()) {
                    selectedIndex_ = (selectedIndex_ - 1 + currentSegmentCandidates_.size()) % currentSegmentCandidates_.size();
                    if (!isCurrentSelectionWhole() && selectedIndex_ < static_cast<int>(candidateCharLengths_.size())) {
                        segmentEdited_ = true;
                        resegmentFromFocused(ic, candidateCharLengths_[selectedIndex_], currentSegmentCandidates_[selectedIndex_]);
                    }
                    renderCandidatePopup(ic);
                    renderSegmentsPreedit(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (isEnter) {
                commitCurrentSelection(ic);
                event.filterAndAccept();
                return;
            }

            if (isEscape) {
                if (selectedIndex_ > 0) {
                    selectedIndex_ = 0;
                    showCandidateList(ic);
                    renderSegmentsPreedit(ic);
                } else {
                    cancelConversionToInputting(ic);
                }
                event.filterAndAccept();
                return;
            }

            if (isShiftUppercaseLetter) {
                commitCurrentSelection(ic);
                insertShiftUppercase(ic, shiftUppercaseChar);
                event.filterAndAccept();
                return;
            }

            if (!hasCtrl && !hasAlt && sym >= 0x20 && sym <= 0x7e) {
                commitCurrentSelection(ic);
                char c = static_cast<char>(sym);
                rawInput_ += c;
                auto res = romajiConverter_.feed(c);
                if (!res.committedKana.empty()) currentPreedit_ += res.committedKana;
                renderInputtingPreedit(ic);
                event.filterAndAccept();
                return;
            }
        }
    } catch (const std::exception& e) {
        FCITX_ERROR() << "[MinimalAddon] Exception caught in keyEvent firewall: " << e.what();
    } catch (...) {
        FCITX_ERROR() << "[MinimalAddon] Unknown exception caught in keyEvent firewall.";
    }

private:
    fcitx::AddonManager* manager_;
    minimal_ime::HistoryManager history_;
    minimal_ime::RomajiConverter romajiConverter_;

    bool updatingPreview_ = false;
    bool segmentEdited_ = false;
    int selectedIndex_ = 0;
    int focusedSegmentIndex_ = 0;

    std::string rawInput_;
    std::string currentPreedit_;
    std::vector<minimal_ime::ViterbiSegment> currentSegments_;
    std::set<int> forcedBoundaries_;
    ImeMode currentMode_ = ImeMode::INPUTTING;
    std::vector<std::string> currentSegmentCandidates_;
    std::vector<int> candidateCharLengths_;
    std::vector<bool> currentCandidateIsWhole_;
};

class MinimalAddonFactory : public fcitx::AddonFactory {
    fcitx::AddonInstance* create(fcitx::AddonManager* manager) override {
        return new MinimalAddon(manager);
    }
};

FCITX_ADDON_FACTORY(MinimalAddonFactory);
