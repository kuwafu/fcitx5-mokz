#pragma once

#include <string>
#include <vector>
#include <string_view>
#include <algorithm>

namespace mokz {

class TypoCorrector {
public:
    static std::vector<std::string> splitUtf8(const std::string& str) {
        std::vector<std::string> chars;
        for (size_t i = 0; i < str.size();) {
            unsigned char c = static_cast<unsigned char>(str[i]);
            size_t len = 1;
            if ((c & 0x80) == 0) len = 1;
            else if ((c & 0xE0) == 0xC0) len = 2;
            else if ((c & 0xF0) == 0xE0) len = 3;
            else if ((c & 0xF8) == 0xF0) len = 4;
            if (i + len > str.size()) break;
            chars.push_back(str.substr(i, len));
            i += len;
        }
        return chars;
    }

    static std::string join(const std::vector<std::string>& chars) {
        std::string res;
        for (const auto& c : chars) res += c;
        return res;
    }

    static std::vector<std::string> getVariants(const std::string& preedit) {
        std::vector<std::string> variants;
        if (preedit.empty()) return variants;

        auto chars = splitUtf8(preedit);
        size_t n = chars.size();
        if (n == 0) return variants;

        // 1. 拗音・促音の小文字化（きようと -> きょうと, がつこう -> がっこう）
        bool hasSmallable = false;
        auto modChars = chars;
        for (size_t i = 0; i < n; ++i) {
            if (modChars[i] == "つ") { modChars[i] = "っ"; hasSmallable = true; }
            else if (modChars[i] == "や") { modChars[i] = "ゃ"; hasSmallable = true; }
            else if (modChars[i] == "ゆ") { modChars[i] = "ゅ"; hasSmallable = true; }
            else if (modChars[i] == "よ") { modChars[i] = "ょ"; hasSmallable = true; }
        }
        if (hasSmallable) {
            variants.push_back(join(modChars));
        }

        // 2. 促音・拗音の大文字化（きょうと -> きようと）
        bool hasBig = false;
        auto modBig = chars;
        for (size_t i = 0; i < n; ++i) {
            if (modBig[i] == "っ") { modBig[i] = "つ"; hasBig = true; }
            else if (modBig[i] == "ゃ") { modBig[i] = "や"; hasBig = true; }
            else if (modBig[i] == "ゅ") { modBig[i] = "ゆ"; hasBig = true; }
            else if (modBig[i] == "ょ") { modBig[i] = "よ"; hasBig = true; }
        }
        if (hasBig) {
            variants.push_back(join(modBig));
        }

        // 3. 末尾2文字の反転スワップ（高速タイピングによる逆転）
        if (n >= 2) {
            auto swapChars = chars;
            std::swap(swapChars[n - 2], swapChars[n - 1]);
            variants.push_back(join(swapChars));
        }

        // 4. QWERTY 母音隣接キー（i <-> u, o <-> i）段ズレ補正
        if (n >= 2) {
            auto vowelFix = chars;
            if (vowelFix.back() == "う") {
                vowelFix.back() = "い";
                variants.push_back(join(vowelFix));
            } else if (vowelFix.back() == "い") {
                vowelFix.back() = "う";
                variants.push_back(join(vowelFix));
            }
        }

        return variants;
    }
};

} // namespace mokz
