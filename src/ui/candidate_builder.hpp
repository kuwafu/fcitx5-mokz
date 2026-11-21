#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <unordered_set>
#include <algorithm>
#include <utility>
#include "input/kana_utils.hpp"
#include "nlp/viterbi.hpp"
#include "storage/history_manager.hpp"

namespace minimal_ime {

class CandidateBuilder {
public:
    static void buildInputtingSuggestions(
        const std::string& evalYomi,
        const std::string& rawInput,
        const HistoryManager& history,
        const std::set<int>& forcedBoundaries,
        std::vector<std::string>& outCandidates,
        std::vector<bool>& outIsWhole)
    {
        outCandidates.clear();
        outIsWhole.clear();
        if (evalYomi.empty()) return;

        std::unordered_set<std::string> seen;
        auto pushCandidate = [&](const std::string& w) {
            if (!w.empty() && seen.insert(w).second) {
                outCandidates.push_back(w);
                outIsWhole.push_back(true);
            }
        };

        // 1. 完全一致する学習履歴
        for (const auto& dw : history.decay().getWordsForExact(evalYomi)) {
            pushCandidate(dw);
        }
        for (const auto& entry : history.commitHistory()) {
            if (entry.first == evalYomi) pushCandidate(entry.second);
        }

        // 2. リアルタイム Viterbi 変換結果
        auto liveSegments = ViterbiEngine::convertToSegments(
            evalYomi, history.commitHistory(), history.bigramHistory(),
            forcedBoundaries, history.lastCommittedWord());
        std::string fullViterbi;
        for (const auto& seg : liveSegments) {
            fullViterbi += seg.word;
        }
        if (!fullViterbi.empty()) {
            pushCandidate(fullViterbi);
        }

        // 3. Stage1Engine による上位変換候補
        if (ViterbiEngine::ensureEngines()) {
            std::vector<int> fbVec(forcedBoundaries.begin(), forcedBoundaries.end());
            std::vector<std::string> cands;
            std::vector<int32_t> costs;
            try {
                ViterbiEngine::stage1Engine().decodeTopN(
                    evalYomi, 8, cands, &costs, &fbVec, nullptr);
            } catch (...) {
                cands.clear();
            }
            std::vector<std::pair<int32_t, std::string>> scored;
            scored.reserve(cands.size());
            for (size_t i = 0; i < cands.size(); ++i) {
                scored.push_back({i < costs.size() ? costs[i] : 0, cands[i]});
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& item : scored) {
                pushCandidate(item.second);
                if (outCandidates.size() >= 6) break;
            }
        }

        // 4. 短い単語のみの前方一致サジェスト (+4文字/12バイト以内)
        for (const auto& dw : history.decay().getWordsForPrefix(evalYomi)) {
            pushCandidate(dw);
        }
        for (const auto& entry : history.commitHistory()) {
            if (entry.first.size() > evalYomi.size() &&
                entry.first.size() <= evalYomi.size() + 12 &&
                entry.first.rfind(evalYomi, 0) == 0) {
                pushCandidate(entry.second);
            }
        }

        pushCandidate(KanaUtils::applyFnConversion(evalYomi, rawInput, FcitxKey_F7));
        pushCandidate(evalYomi);
    }

    static void buildConversionCandidates(
        const std::string& currentPreedit,
        const std::string& rawInput,
        const std::vector<ViterbiSegment>& currentSegments,
        int focusedSegmentIndex,
        bool segmentEdited,
        const HistoryManager& history,
        std::vector<std::string>& outCandidates,
        std::vector<bool>& outIsWhole)
    {
        outCandidates.clear();
        outIsWhole.clear();
        if (currentSegments.empty()) return;

        std::unordered_set<std::string> seen;
        auto pushCandidate = [&](const std::string& w, bool isWhole) {
            if (!w.empty() && seen.insert(w).second) {
                outCandidates.push_back(w);
                outIsWhole.push_back(isWhole);
            }
        };

        std::string fullViterbiSentence;
        for (const auto& seg : currentSegments) {
            fullViterbiSentence += seg.word;
        }
        std::string fullKata = KanaUtils::applyFnConversion(currentPreedit, rawInput, FcitxKey_F7);

        // 1. 初回変換時（個別文節操作に入る前）は、全文一括候補を先頭に配置
        if (!segmentEdited && focusedSegmentIndex == 0) {
            if (!fullViterbiSentence.empty()) {
                pushCandidate(fullViterbiSentence, true);
            }
            for (const auto& dw : history.decay().getWordsForExact(currentPreedit)) {
                pushCandidate(dw, true);
            }
            if (currentSegments.size() <= 1) {
                pushCandidate(fullKata, true);
                pushCandidate(currentPreedit, true);
            }
        }

        // 2. 現在フォーカス中の文節に対する候補
        bool segIsWhole = (currentSegments.size() <= 1);
        if (focusedSegmentIndex >= 0 && focusedSegmentIndex < static_cast<int>(currentSegments.size())) {
            const auto& activeSeg = currentSegments[focusedSegmentIndex];
            const std::string& stemYomi = activeSeg.stemYomi.empty() ? activeSeg.yomi : activeSeg.stemYomi;
            const std::string& suffixWord = activeSeg.suffixWord;

            pushCandidate(activeSeg.word, segIsWhole);

            for (const auto& dw : history.decay().getWordsForExact(activeSeg.yomi)) {
                pushCandidate(dw, segIsWhole);
            }
            if (!suffixWord.empty()) {
                for (const auto& dw : history.decay().getWordsForExact(stemYomi)) {
                    pushCandidate(dw + suffixWord, segIsWhole);
                }
            }

            for (const auto& entry : history.commitHistory()) {
                if (entry.first == stemYomi) {
                    pushCandidate(entry.second + suffixWord, segIsWhole);
                } else if (entry.first == activeSeg.yomi) {
                    pushCandidate(entry.second, segIsWhole);
                }
            }

            auto searchStage1Exact = [&](const std::string& qYomi, const std::string& tailSuffix) {
                if (qYomi.empty() || !ViterbiEngine::ensureEngines()) return;
                std::vector<std::string> cands;
                std::vector<int32_t> costs;
                try {
                    ViterbiEngine::stage1Engine().decodeTopN(qYomi, 8, cands, &costs, nullptr, nullptr);
                } catch (...) {
                    return;
                }
                std::vector<std::pair<int32_t, std::string>> scored;
                scored.reserve(cands.size());
                for (size_t i = 0; i < cands.size(); ++i) {
                    scored.push_back({i < costs.size() ? costs[i] : 0, cands[i]});
                }
                std::sort(scored.begin(), scored.end(),
                          [](const auto& a, const auto& b) { return a.first < b.first; });
                for (const auto& item : scored) {
                    std::string cand = item.second;
                    if (!tailSuffix.empty()) {
                        if (cand.size() < tailSuffix.size() ||
                            cand.compare(cand.size() - tailSuffix.size(), tailSuffix.size(), tailSuffix) != 0) {
                            cand += tailSuffix;
                        }
                    }
                    pushCandidate(cand, segIsWhole);
                }
            };

            searchStage1Exact(stemYomi, suffixWord);

            static const char* const kInflections[] = {
                "ました", "ません", "ている", "ていた", "ていて",
                "した", "して", "ます", "です", "ない", "たい", "から", "まで",
                "た", "て", "だ", "で", "は", "が", "を", "に", "へ", "と", "も", "の"
            };
            for (const char* inf : kInflections) {
                std::string_view infView(inf);
                if (activeSeg.yomi.size() > infView.size() + 3 &&
                    activeSeg.yomi.compare(activeSeg.yomi.size() - infView.size(), infView.size(), infView) == 0) {
                    std::string subStem = activeSeg.yomi.substr(0, activeSeg.yomi.size() - infView.size());
                    if (subStem != stemYomi) {
                        searchStage1Exact(subStem, std::string(infView));
                    }
                }
            }

            pushCandidate(activeSeg.yomi, segIsWhole);
            pushCandidate(KanaUtils::applyFnConversion(activeSeg.yomi, "", FcitxKey_F7), segIsWhole);
        }

        if (!segmentEdited && currentSegments.size() > 1) {
            pushCandidate(fullKata, true);
            pushCandidate(currentPreedit, true);
        }
    }

    static void buildConversionCandidatesDynamic(
        const std::string& currentPreedit,
        const std::string& rawInput,
        const std::vector<ViterbiSegment>& currentSegments,
        int focusedSegmentIndex,
        bool segmentEdited,
        const HistoryManager& history,
        std::vector<std::string>& outCandidates,
        std::vector<int>& outCandidateCharLengths,
        std::vector<bool>& outIsWhole)
    {
        outCandidates.clear();
        outCandidateCharLengths.clear();
        outIsWhole.clear();
        if (currentSegments.empty()) return;

        const int totalChars = static_cast<int>(KanaUtils::countUtf8Chars(currentPreedit));
        const int activeLen = (focusedSegmentIndex >= 0 && focusedSegmentIndex < static_cast<int>(currentSegments.size()))
            ? currentSegments[focusedSegmentIndex].endChar - currentSegments[focusedSegmentIndex].startChar
            : totalChars;

        auto pushCandidate = [&](const std::string& w, int len, bool isWhole) {
            if (w.empty()) return;
            for (size_t i = 0; i < outCandidates.size(); ++i) {
                if (outCandidates[i] == w) return;
            }
            outCandidates.push_back(w);
            outCandidateCharLengths.push_back(std::max(1, len));
            outIsWhole.push_back(isWhole);
        };

        if (!ViterbiEngine::ensureEngines()) {
            buildConversionCandidates(
                currentPreedit, rawInput, currentSegments, focusedSegmentIndex, segmentEdited,
                history, outCandidates, outIsWhole);
            outCandidateCharLengths.clear();
            for (bool isWhole : outIsWhole) {
                outCandidateCharLengths.push_back(isWhole ? totalChars : activeLen);
            }
            return;
        }

        std::string fullViterbi;
        for (const auto& seg : currentSegments) fullViterbi += seg.word;

        if (!segmentEdited && focusedSegmentIndex == 0 && !fullViterbi.empty()) {
            pushCandidate(fullViterbi, totalChars, true);
        }

        if (focusedSegmentIndex < 0 || focusedSegmentIndex >= static_cast<int>(currentSegments.size())) {
            pushCandidate(currentPreedit, totalChars, true);
            return;
        }

        const auto& activeSeg = currentSegments[focusedSegmentIndex];
        const bool segIsWhole = (currentSegments.size() <= 1);
        const int segLen = activeSeg.endChar - activeSeg.startChar;

        pushCandidate(activeSeg.word, segLen, segIsWhole);

        for (const auto& dw : history.decay().getWordsForExact(activeSeg.yomi)) {
            pushCandidate(dw, segLen, segIsWhole);
        }

        const std::string suffixWord = activeSeg.suffixWord;
        const std::string stemYomi = activeSeg.stemYomi.empty() ? activeSeg.yomi : activeSeg.stemYomi;

        if (!suffixWord.empty()) {
            for (const auto& dw : history.decay().getWordsForExact(stemYomi)) {
                pushCandidate(dw + suffixWord, segLen, segIsWhole);
            }
        }

        std::string subYomi = suffixWord.empty() ? activeSeg.yomi : stemYomi;
        bool appendSuffix = !suffixWord.empty() && !activeSeg.stemYomi.empty();

        if (!subYomi.empty()) {
            std::vector<std::string> cands;
            std::vector<int32_t> costs;
            std::vector<std::vector<hp_ime::WordSegment>> segLists;
            ViterbiEngine::stage1Engine().decodeTopN(subYomi, 8, cands, &costs, nullptr, &segLists);

            std::vector<std::pair<int32_t, std::string>> scored;
            scored.reserve(cands.size());
            for (size_t i = 0; i < cands.size(); ++i) {
                int32_t c = (i < costs.size()) ? costs[i] : 0;
                scored.push_back({c, cands[i]});
            }
            std::sort(scored.begin(), scored.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            for (const auto& p : scored) {
                std::string cand = p.second;
                if (appendSuffix) {
                    if (cand.size() < suffixWord.size() ||
                        cand.compare(cand.size() - suffixWord.size(), suffixWord.size(), suffixWord) != 0) {
                        cand += suffixWord;
                    }
                }
                pushCandidate(cand, segLen, segIsWhole);
            }
        }

        pushCandidate(activeSeg.yomi, segLen, segIsWhole);
        pushCandidate(KanaUtils::applyFnConversion(activeSeg.yomi, "", FcitxKey_F7), segLen, segIsWhole);

        if (!segmentEdited && currentSegments.size() > 1) {
            pushCandidate(KanaUtils::applyFnConversion(currentPreedit, rawInput, FcitxKey_F7), totalChars, true);
            pushCandidate(currentPreedit, totalChars, true);
        }
    }
};

} // namespace minimal_ime
