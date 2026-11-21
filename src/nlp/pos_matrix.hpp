#pragma once

#include <string>
#include <string_view>
#include <cstdint>

namespace minimal_ime {

enum POS : uint8_t {
    POS_BOS = 0,
    POS_NOUN = 1,
    POS_PARTICLE = 2,
    POS_VERB = 3,
    POS_ADJ = 4,
    POS_AUX = 5,
    POS_PREFIX = 6,
    POS_PUNCT = 7,
    POS_OTHER = 8
};

class PosClassifier {
public:
    static bool isPureHiragana(std::string_view s) {
        if (s.empty() || s.size() % 3 != 0) return false;
        for (size_t i = 0; i < s.size(); i += 3) {
            unsigned char b0 = static_cast<unsigned char>(s[i]);
            unsigned char b1 = static_cast<unsigned char>(s[i + 1]);
            unsigned char b2 = static_cast<unsigned char>(s[i + 2]);
            if (b0 != 0xE3) return false;
            if (b1 == 0x81 && b2 >= 0x81 && b2 <= 0xBF) continue;
            if (b1 == 0x82 && b2 >= 0x80 && b2 <= 0x96) continue;
            return false;
        }
        return true;
    }

    static POS classify(std::string_view word, std::string_view yomi) {
        if (word == "、" || word == "。" || word == "！" || word == "？" || word == "・") return POS_PUNCT;
        if (word == "は" || word == "が" || word == "を" || word == "に" ||
            word == "へ" || word == "と" || word == "で" || word == "から" ||
            word == "より" || word == "も" || word == "の" || word == "ね" ||
            word == "よ" || word == "な" || word == "か") {
            return POS_PARTICLE;
        }
        if (word == "だ" || word == "です" || word == "ます" || word == "た" ||
            word == "て" || word == "で" || word == "した" || word == "して" ||
            word == "いて" || word == "いる" || word == "いた" || word == "ない" ||
            word == "たい" || word == "れる" || word == "られる" || word == "そう" || word == "よう") {
            return POS_AUX;
        }
        if (word == "お" || word == "ご" || word == "御" || word == "最" || word == "第" || word == "超") {
            return POS_PREFIX;
        }

        // 漢字を含む語のうち、送り仮名（末尾がひらがな）を持つものは動詞・形容詞として判定
        if (yomi.size() >= 6 && word.size() >= 6 && !isPureHiragana(word)) {
            std::string_view wordTail = word.substr(word.size() - 3);
            if (isPureHiragana(wordTail)) {
                if (wordTail == "い") {
                    return POS_ADJ;
                }
                // 終止形・連用形・音便形（例: 試す、試し、降っ、食べ、書い）
                if (wordTail == "う" || wordTail == "く" || wordTail == "ぐ" || wordTail == "す" ||
                    wordTail == "つ" || wordTail == "ぬ" || wordTail == "ぶ" || wordTail == "む" || wordTail == "る" ||
                    wordTail == "き" || wordTail == "ぎ" || wordTail == "し" || wordTail == "ち" ||
                    wordTail == "に" || wordTail == "び" || wordTail == "み" || wordTail == "り" ||
                    wordTail == "え" || wordTail == "け" || wordTail == "せ" || wordTail == "て" ||
                    wordTail == "ね" || wordTail == "へ" || wordTail == "め" || wordTail == "れ" ||
                    wordTail == "っ" || wordTail == "ん" || wordTail == "た" || wordTail == "だ" || wordTail == "데") {
                    return POS_VERB;
                }
            }
        }
        return POS_NOUN;
    }

    static inline int16_t getTransitionCost(POS prev, POS curr) {
        static constexpr int16_t MATRIX[9][9] = {
            /*             BOS   NOUN  PART  VERB   ADJ   AUX  PREF  PUNC OTHER */
            /* BOS    */ { 9999,  200, 4500,  250,  250, 5000,  200, 9999, 1000 },
            /* NOUN   */ { 9999,  950,  100, 1100, 1300,  250, 2000,  100, 1000 },
            /* PART   */ { 9999,  200, 3500,  100,  150, 2500,  600,  400, 1000 },
            /* VERB   */ { 9999,  600,  250, 1200, 1500,    0, 2000,  200, 1000 },
            /* ADJ    */ { 9999,  500,  250, 1500, 1500,  100, 2000,  200, 1000 },
            /* AUX    */ { 9999,  900,  150,  800, 1000,   50, 2000,  100, 1000 },
            /* PREFIX */ { 9999,  100, 5000, 2500, 2500, 5000, 4000, 9999, 2000 },
            /* PUNCT  */ { 9999,  200, 4000,  300,  300, 5000,  300, 9999, 1000 },
            /* OTHER  */ { 9999,  800,  400,  800,  800,  800, 1200,  400, 1000 }
        };

        if (prev <= POS_OTHER && curr <= POS_OTHER) {
            return MATRIX[prev][curr];
        }
        return 1000;
    }
};

} // namespace minimal_ime
