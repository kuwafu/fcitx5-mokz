#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <set>
#include <unordered_set>
#include <algorithm>
#include "input/kana_utils.hpp"
#include "nlp/viterbi.hpp"
#include "storage/history_manager.hpp"

namespace mokz {

struct SegmentCandidateItem {
    std::string word;
    int charLength; // この候補が消費する読み文字数
    bool isWhole;
};

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

        for (const auto& dw : history.decay().getWordsForExact(evalYomi)) {
            pushCandidate(dw);
        }
        for (const auto& entry : history.commitHistory()) {
            if (entry.first == evalYomi) pushCandidate(entry.second);
        }

        auto liveSegments = ViterbiEngine::convertToSegments(
            evalYomi, history.commitHistory(), history.bigramHistory(),
            forcedBoundaries, history.lastCommittedWord());
        std::string fullViterbi;
        for (const auto& seg : liveSegments) {
            fullViterbi += seg.word;
        }
        if (!fullViterbi.empty()) pushCandidate(fullViterbi);

        pushCandidate(KanaUtils::applyFnConversion(evalYomi, rawInput, FcitxKey_F7));
        pushCandidate(evalYomi);
    }

    static void buildConversionCandidatesDynamic(
        const std::string& currentPreedit,
        const std::string& rawInput,
        const std::vector<ViterbiSegment>& currentSegments,
        int focusedSegmentIndex,
        bool segmentEdited,
        const HistoryManager& history,
        std::vector<std::string>& outCandidates,
        std::vector<int>& outCharLengths,
        std::vector<bool>& outIsWhole)
    {
        outCandidates.clear();
        outCharLengths.clear();
        outIsWhole.clear();
        if (currentSegments.empty()) return;

        std::unordered_set<std::string> seen;
        auto pushCand = [&](const std::string& w, int charLen, bool isWhole) {
            if (!w.empty() && seen.insert(w).second) {
                outCandidates.push_back(w);
                outCharLengths.push_back(charLen);
                outIsWhole.push_back(isWhole);
            }
        };

        // 1. 初回変換時、全文一括候補を先頭に
        std::string fullViterbi;
        for (const auto& seg : currentSegments) fullViterbi += seg.word;
        if (!segmentEdited && focusedSegmentIndex == 0 && !fullViterbi.empty()) {
            int totalChars = static_cast<int>(KanaUtils::countUtf8Chars(currentPreedit));
            pushCand(fullViterbi, totalChars, true);
        }

        if (focusedSegmentIndex < 0 || focusedSegmentIndex >= static_cast<int>(currentSegments.size())) return;

        const auto& activeSeg = currentSegments[focusedSegmentIndex];
        int baseStartChar = activeSeg.startChar;

        // 全文を文字単位でオフセット取得
        std::vector<size_t> charOffsets;
        for (size_t i = 0; i <= currentPreedit.size(); ++i) {
            if (i == currentPreedit.size() || (static_cast<unsigned char>(currentPreedit[i]) & 0xC0) != 0x80) {
                charOffsets.push_back(i);
            }
        }
        int totalChars = static_cast<int>(charOffsets.size()) - 1;

        int curLen = activeSeg.endChar - activeSeg.startChar;
        // 探索する文節長の候補 (現在長, 現在長+1, 現在長-1, 現在長+2, 現在長-2 ...)
        std::vector<int> targetLens;
        targetLens.push_back(curLen);
        for (int d = 1; d <= 4; ++d) {
            if (curLen + d <= (totalChars - baseStartChar)) targetLens.push_back(curLen + d);
            if (curLen - d >= 1) targetLens.push_back(curLen - d);
        }

        struct CandidateItem {
            std::string word;
            int charLength;
            int cost;
            bool isWhole;
        };

        std::vector<std::vector<CandidateItem>> pools(targetLens.size());

        // 各文字長ごとに候補プールを一時収集し、コスト昇順で整列する。
        for (size_t poolIndex = 0; poolIndex < targetLens.size(); ++poolIndex) {
            const int l = targetLens[poolIndex];
            int endChar = baseStartChar + l;
            size_t sb = charOffsets[baseStartChar];
            size_t eb = charOffsets[endChar];
            std::string subYomi = currentPreedit.substr(sb, eb - sb);

            auto& pool = pools[poolIndex];
            auto addToPool = [&](const std::string& w, int cost) {
                if (!w.empty()) {
                    pool.push_back({w, l, cost, false});
                }
            };

            // 学習履歴
            for (const auto& dw : history.decay().getWordsForExact(subYomi)) {
                addToPool(dw, 0);
            }
            for (const auto& entry : history.commitHistory()) {
                if (entry.first == subYomi) {
                    addToPool(entry.second, 1);
                }
            }

            // Stage1Engine 辞書
            if (ViterbiEngine::ensureEngines()) {
                std::vector<std::string> cands;
                std::vector<int32_t> costs;
                try {
                    ViterbiEngine::stage1Engine().decodeTopN(subYomi, 8, cands, &costs, nullptr, nullptr);
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
                for (size_t i = 0; i < std::min<size_t>(6, scored.size()); ++i) {
                    addToPool(scored[i].second, static_cast<int>(scored[i].first) + 10);
                }
            }

            // かな/カナ
            addToPool(subYomi, 1000000);
            addToPool(KanaUtils::applyFnConversion(subYomi, "", FcitxKey_F7), 1000001);

            std::stable_sort(
                pool.begin(),
                pool.end(),
                [](const CandidateItem& a, const CandidateItem& b) {
                    return a.cost < b.cost;
                }
            );

            pool.erase(
                std::unique(
                    pool.begin(),
                    pool.end(),
                    [](const CandidateItem& a, const CandidateItem& b) {
                        return a.word == b.word;
                    }
                ),
                pool.end()
            );
        }

        // 各文字長プールから 1 件ずつ取り出し、異なる文字長の候補が交互に並ぶようにする。
        size_t maxDepth = 0;
        for (const auto& pool : pools) {
            maxDepth = std::max(maxDepth, pool.size());
        }

        for (size_t depth = 0; depth < maxDepth && outCandidates.size() < 18; ++depth) {
            for (const auto& pool : pools) {
                if (depth >= pool.size()) continue;

                const auto& item = pool[depth];
                pushCand(item.word, item.charLength, item.isWhole);

                if (outCandidates.size() >= 18) break;
            }
        }
    }
};

} // namespace mokz
