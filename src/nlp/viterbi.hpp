#pragma once

#include <string>
#include <vector>
#include <deque>
#include <map>
#include <set>
#include <algorithm>
#include <string_view>
#include <charconv>
#include <cstdint>
#include <marisa.h>
#include <limits>
#include "pos_matrix.hpp"
#include "typo.hpp"
#include "stage1_engine.hpp"
#include "reranker_engine.hpp"

namespace mokz {

inline const std::set<int>& emptyIntSet() {
    static const std::set<int> empty;
    return empty;
}

inline const std::string& emptyString() {
    static const std::string empty;
    return empty;
}

struct ViterbiNode {
    int startChar;
    int endChar;
    std::string word;
    int wordCost;
    POS pos = POS_OTHER;
};

struct ViterbiSegment {
    int startChar;
    int endChar;
    std::string word;
    std::string yomi;
    std::string stemYomi;
    std::string suffixWord;
};

class ViterbiEngine {
private:
    static hp_ime::Stage1Engine& stage1EngineNoInit() {
        static hp_ime::Stage1Engine engine;
        return engine;
    }

    static hp_ime::RerankerEngine& rerankerEngineNoInit() {
        static hp_ime::RerankerEngine engine;
        return engine;
    }

public:
    static constexpr int INF_COST = 100000000;

    static bool ensureEngines() {
        static const bool ok = [] {
            const std::string dir = "/usr/share/fcitx5/mokz/";
            const std::string dict = dir + "dict.bin";
            const std::string kenlm = dir + "kenlm.binary";
            const std::string reranker = dir + "reranker.bin";

            if (!stage1EngineNoInit().init(dict.c_str(), kenlm.c_str())) return false;
            if (!rerankerEngineNoInit().init(reranker.c_str())) return false;
            return true;
        }();
        return ok;
    }

    static hp_ime::Stage1Engine& stage1Engine() {
        ensureEngines();
        return stage1EngineNoInit();
    }

    static hp_ime::RerankerEngine& rerankerEngine() {
        ensureEngines();
        return rerankerEngineNoInit();
    }

    static std::vector<ViterbiSegment> convertToSegments(
        const std::string& preedit,
        const std::deque<std::pair<std::string, std::string>>& commitHistory,
        const std::map<std::string, std::vector<std::string>>& bigramHistory,
        const std::set<int>& forcedBoundaries = emptyIntSet(),
        const std::string& prevCommittedWord = emptyString())
    {
        static marisa::Trie ignoredTrie;
        return convertToSegments(
            preedit, ignoredTrie, commitHistory, bigramHistory,
            forcedBoundaries, prevCommittedWord);
    }

    static std::vector<ViterbiSegment> convertToSegments(
        const std::string& preedit,
        const marisa::Trie& trie,
        const std::deque<std::pair<std::string, std::string>>& commitHistory,
        const std::map<std::string, std::vector<std::string>>& bigramHistory,
        const std::set<int>& forcedBoundaries = emptyIntSet(),
        const std::string& prevCommittedWord = emptyString())
    {
        (void)trie;
        (void)commitHistory;
        (void)bigramHistory;
        (void)prevCommittedWord;

        if (preedit.empty() || !ensureEngines()) {
            return {};
        }

        std::vector<size_t> charOffsets;
        for (size_t i = 0; i <= preedit.size(); ++i) {
            if (i == preedit.size() || (static_cast<unsigned char>(preedit[i]) & 0xC0) != 0x80) {
                charOffsets.push_back(i);
            }
        }

        size_t numChars = charOffsets.size() - 1;
        if (numChars == 0) return {};

        auto makeFallback = [&]() -> std::vector<ViterbiSegment> {
            return {ViterbiSegment{0, static_cast<int>(numChars), preedit, preedit, preedit, ""}};
        };

        std::vector<int> fbVec;
        fbVec.reserve(forcedBoundaries.size());
        for (int b : forcedBoundaries) fbVec.push_back(b);

        std::vector<std::string> cands;
        std::vector<int32_t> costs;
        std::vector<std::vector<hp_ime::WordSegment>> segLists;
        stage1Engine().decodeTopN(preedit, 8, cands, &costs, &fbVec, &segLists);
        if (cands.empty()) return makeFallback();

        std::vector<float> scores(cands.size(), 0.0f);
        size_t eval_nodes = 0;
        rerankerEngine().scoreCandidatesTrie(cands, scores.data(), eval_nodes);

        size_t best_idx = 0;
        int32_t best_score = std::numeric_limits<int32_t>::max();
        for (size_t i = 0; i < cands.size(); ++i) {
            int32_t combined = 0;
            if (i < costs.size()) {
                combined = costs[i] - static_cast<int32_t>(scores[i] * 600.0f);
            } else {
                combined = -static_cast<int32_t>(scores[i] * 600.0f);
            }
            if (combined < best_score) {
                best_score = combined;
                best_idx = i;
            }
        }

        struct RawSegment {
            int startChar;
            int endChar;
            std::string word;
            std::string yomi;
            std::string stemYomi;
            std::string suffixWord;
            POS pos;
        };

        std::vector<RawSegment> rawSegments;
        if (best_idx < segLists.size() && !segLists[best_idx].empty()) {
            for (const auto& ws : segLists[best_idx]) {
                int startChar = std::clamp(ws.start_char, 0, static_cast<int>(numChars));
                int endChar = std::clamp(ws.start_char + ws.char_length, startChar, static_cast<int>(numChars));
                if (endChar <= startChar) continue;

                std::string yomi = ws.yomi;
                if (yomi.empty()) {
                    size_t sb = charOffsets[static_cast<size_t>(startChar)];
                    size_t eb = charOffsets[static_cast<size_t>(endChar)];
                    yomi = preedit.substr(sb, eb - sb);
                }
                std::string surface = ws.surface.empty() ? yomi : ws.surface;
                rawSegments.push_back(RawSegment{
                    startChar, endChar, surface, yomi, yomi, "", classifyWord(surface, yomi)
                });
            }
        }

        if (!rawSegments.empty()) {
            bool contiguous = true;
            int expected = 0;
            for (const auto& s : rawSegments) {
                if (s.startChar != expected) {
                    contiguous = false;
                    break;
                }
                expected = s.endChar;
            }
            if (!contiguous || expected != static_cast<int>(numChars)) {
                rawSegments.clear();
            }
        }

        if (rawSegments.empty()) {
            rawSegments.push_back(RawSegment{
                0, static_cast<int>(numChars), cands[best_idx], preedit, preedit, "",
                classifyWord(cands[best_idx], preedit)
            });
        }

        std::vector<RawSegment> chunks;
        chunks.reserve(rawSegments.size());

        for (const auto& seg : rawSegments) {
            bool merged = false;

            if (!chunks.empty() && forcedBoundaries.count(seg.startChar) == 0) {
                auto& back = chunks.back();

                bool mergeFunction = isFunctionPos(seg.pos);
                bool mergePrefix = (back.pos == POS_PREFIX);
                bool mergeVerbAux = (back.pos == POS_VERB && seg.pos == POS_AUX);

                if (mergeFunction || mergePrefix || mergeVerbAux) {
                    back.endChar = seg.endChar;
                    back.word += seg.word;
                    back.yomi += seg.yomi;

                    if (isFunctionPos(seg.pos)) {
                        back.suffixWord += seg.word;
                    }

                    if (back.pos == POS_PREFIX) {
                        back.pos = seg.pos;
                    }

                    merged = true;
                }
            }

            if (!merged) {
                chunks.push_back(seg);
            }
        }

        std::vector<ViterbiSegment> segments;
        segments.reserve(chunks.size());

        for (const auto& chunk : chunks) {
            segments.push_back(ViterbiSegment{
                chunk.startChar, chunk.endChar, chunk.word, chunk.yomi,
                chunk.stemYomi, chunk.suffixWord
            });
        }

        return segments;
    }

    static std::string convert(
        const std::string& preedit,
        const marisa::Trie& trie,
        const std::deque<std::pair<std::string, std::string>>& commitHistory,
        const std::map<std::string, std::vector<std::string>>& bigramHistory)
    {
        std::string result;
        for (const auto& seg : convertToSegments(preedit, trie, commitHistory, bigramHistory)) {
            result += seg.word;
        }
        return result;
    }

    static bool isSymbolYomi(std::string_view yomi) {
        return yomi == "・" || yomi == "、" || yomi == "。" ||
               yomi == "！" || yomi == "？" || yomi == "〜" || yomi == "ー";
    }

    static bool isParticleYomi(std::string_view yomi) {
        return yomi == "は" || yomi == "が" || yomi == "を" ||
               yomi == "に" || yomi == "で" || yomi == "と" ||
               yomi == "へ" || yomi == "も" || yomi == "の" ||
               yomi == "や" || yomi == "か" || yomi == "ね" ||
               yomi == "よ" || yomi == "わ";
    }

    static bool isAttachableSuffix(std::string_view yomi, std::string_view word) {
        if (isSymbolYomi(yomi)) return true;
        if (word != yomi) return false;
        if (isParticleYomi(yomi)) return true;
        return yomi == "て" || yomi == "た" || yomi == "だ" ||
               yomi == "し" || yomi == "な" || yomi == "ら" ||
               yomi == "い" || yomi == "る" || yomi == "く" ||
               yomi == "した" || yomi == "して" ||
               yomi == "いて" || yomi == "いる" || yomi == "いた" ||
               yomi == "ます" || yomi == "です" || yomi == "ない" ||
               yomi == "たい" || yomi == "から" || yomi == "まで" ||
               yomi == "より" || yomi == "だけ" || yomi == "ほど";
    }

private:
    static bool isFunctionPos(POS p) {
        return p == POS_PARTICLE || p == POS_AUX || p == POS_PUNCT;
    }

    static bool isContentPos(POS p) {
        switch (p) {
        case POS_NOUN:
        case POS_VERB:
        case POS_ADJ:
        case POS_PREFIX:
            return true;
        default:
            return false;
        }
    }

    static int32_t calcSegmentAdjustment(POS prevPos, POS currPos,
                                         size_t yomiBytes, size_t wordBytes,
                                         std::string_view word) {
        int32_t adj = 0;

        // (A) 単語区切りペナルティ（過剰分割の抑制）
        if (isContentPos(currPos)) {
            adj += 550;

            // 単漢字ノードの細切れ誤爆抑制
            if (yomiBytes == 3 && wordBytes == 3 &&
                !PosClassifier::isPureHiragana(word) &&
                prevPos != POS_BOS) {
                adj += 700;
            }
        }

        // (B) 読み長カバレッジボーナス（自然な長語の優遇）
        size_t yomiChars = yomiBytes / 3;
        if (isContentPos(currPos) && yomiChars >= 2) {
            adj -= static_cast<int32_t>(std::min<size_t>(yomiChars - 1, 5) * 220);
        }

        // (C) 文節境界ボーナス（文脈に応じた自動伸縮）
        if (isFunctionPos(prevPos) && isContentPos(currPos)) {
            adj -= 350;
        }

        if (prevPos == POS_VERB && currPos == POS_AUX) {
            adj -= 400;
        }

        return adj;
    }

    static bool endsWith(std::string_view s, std::string_view suffix) {
        return s.size() >= suffix.size() &&
               s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    static POS classifyWord(const std::string& word, const std::string& yomi) {
        POS pos = PosClassifier::classify(word, yomi);
        if (word == yomi && isFunctionPos(pos)) {
            return pos;
        }
        if (endsWith(word, "いた") || endsWith(word, "いだ") || endsWith(word, "した") ||
            endsWith(word, "った") || endsWith(word, "んだ") ||
            endsWith(word, "いて") || endsWith(word, "いで") || endsWith(word, "して") ||
            endsWith(word, "って") || endsWith(word, "んで") ||
            endsWith(yomi, "て") || endsWith(yomi, "で")) {
            return POS_VERB;
        }
        return pos;
    }
};

} // namespace mokz
