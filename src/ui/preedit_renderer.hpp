#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <algorithm>
#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/candidatelist.h>
#include <fcitx/userinterface.h>
#include "nlp/viterbi.hpp"

namespace mokz {

class SuggestWord : public fcitx::CandidateWord {
public:
    using SelectCallback = std::function<void(fcitx::InputContext*, int)>;

    explicit SuggestWord(fcitx::Text text, int index, SelectCallback onSelect)
        : fcitx::CandidateWord(std::move(text)),
          index_(index),
          onSelect_(std::move(onSelect)) {}

    void select(fcitx::InputContext* ic) const override {
        if (ic && onSelect_) {
            onSelect_(ic, index_);
        }
    }

private:
    int index_;
    SelectCallback onSelect_;
};

class PreeditRenderer {
public:
    static constexpr int CANDIDATE_PAGE_SIZE = 9;

    static void setClientOrServerPreedit(fcitx::InputContext* ic, const fcitx::Text& text) {
        if (ic->capabilityFlags().test(fcitx::CapabilityFlag::Preedit)) {
            ic->inputPanel().setClientPreedit(text);
        } else {
            ic->inputPanel().setPreedit(text);
        }
        ic->updatePreedit();
    }

    static void renderCandidatePopup(
        fcitx::InputContext* ic,
        const std::vector<std::string>& candidates,
        int& selectedIndex,
        SuggestWord::SelectCallback onSelect)
    {
        if (!ic) return;
        if (candidates.empty()) {
            ic->inputPanel().setCandidateList(nullptr);
            ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
            return;
        }

        auto candidateList = std::make_unique<fcitx::CommonCandidateList>();
        fcitx::KeyList selectionKeys;
        selectionKeys.emplace_back(FcitxKey_1);
        selectionKeys.emplace_back(FcitxKey_2);
        selectionKeys.emplace_back(FcitxKey_3);
        selectionKeys.emplace_back(FcitxKey_4);
        selectionKeys.emplace_back(FcitxKey_5);
        selectionKeys.emplace_back(FcitxKey_6);
        selectionKeys.emplace_back(FcitxKey_7);
        selectionKeys.emplace_back(FcitxKey_8);
        selectionKeys.emplace_back(FcitxKey_9);
        candidateList->setSelectionKey(selectionKeys);

        int safeIndex = std::clamp(selectedIndex, 0, std::max(0, static_cast<int>(candidates.size()) - 1));
        selectedIndex = safeIndex;
        candidateList->setPageSize(CANDIDATE_PAGE_SIZE);
        candidateList->setLayoutHint(fcitx::CandidateLayoutHint::Vertical);

        for (size_t i = 0; i < candidates.size(); ++i) {
            candidateList->append<SuggestWord>(fcitx::Text(candidates[i]), static_cast<int>(i), onSelect);
        }

        int totalPages = candidateList->totalPages();
        if (totalPages > 0) {
            int targetPage = std::clamp(safeIndex / CANDIDATE_PAGE_SIZE, 0, totalPages - 1);
            candidateList->setPage(targetPage);
        }
        candidateList->setGlobalCursorIndex(safeIndex);

        ic->inputPanel().setCandidateList(std::move(candidateList));
        ic->updateUserInterface(fcitx::UserInterfaceComponent::InputPanel);
    }

    static void renderStage1Preedit(
        fcitx::InputContext* ic,
        const std::vector<std::string>& candidates,
        int selectedIndex,
        bool& updatingPreview)
    {
        if (!ic || candidates.empty()) return;
        int idx = std::clamp(selectedIndex, 0, static_cast<int>(candidates.size()) - 1);
        fcitx::Text text(candidates[idx], fcitx::TextFormatFlag::HighLight);
        updatingPreview = true;
        setClientOrServerPreedit(ic, text);
        updatingPreview = false;
    }

    static void renderSegmentsPreedit(
        fcitx::InputContext* ic,
        const std::vector<ViterbiSegment>& segments,
        int focusedSegmentIndex,
        const std::string& chosen,
        bool isWhole,
        bool& updatingPreview)
    {
        if (!ic || segments.empty()) return;
        fcitx::Text text;

        if (!chosen.empty() && isWhole) {
            text.append(chosen, fcitx::TextFormatFlag::HighLight);
        } else {
            for (size_t i = 0; i < segments.size(); ++i) {
                std::string w = (static_cast<int>(i) == focusedSegmentIndex && !chosen.empty())
                                    ? chosen
                                    : segments[i].word;
                if (static_cast<int>(i) == focusedSegmentIndex) {
                    text.append(w, fcitx::TextFormatFlag::HighLight);
                } else {
                    text.append(w, fcitx::TextFormatFlag::Underline);
                }
            }
        }

        updatingPreview = true;
        setClientOrServerPreedit(ic, text);
        updatingPreview = false;
    }
};

} // namespace mokz
