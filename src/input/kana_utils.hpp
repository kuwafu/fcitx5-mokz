#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <cstdint>
#include <fcitx-utils/key.h>

namespace minimal_ime {

class KanaUtils {
public:
    static void popBackUtf8(std::string& s) {
        if (s.empty()) return;
        while (!s.empty()) {
            char c = s.back();
            s.pop_back();
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) break;
        }
    }

    static size_t countUtf8Chars(std::string_view s) {
        size_t count = 0;
        for (unsigned char c : s) {
            if ((c & 0xC0) != 0x80) count++;
        }
        return count;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | ((cp >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | ((cp >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | ((cp >> 18) & 0x07)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    static uint32_t decodeUtf8(const std::string& s, size_t& pos) {
        if (pos >= s.size()) return 0;

        unsigned char c0 = static_cast<unsigned char>(s[pos]);
        if (c0 < 0x80) {
            ++pos;
            return c0;
        }

        if ((c0 & 0xE0) == 0xC0 && pos + 1 < s.size()) {
            uint32_t cp = ((static_cast<uint32_t>(c0) & 0x1F) << 6) |
                          (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1])) & 0x3F);
            pos += 2;
            return cp;
        }

        if ((c0 & 0xF0) == 0xE0 && pos + 2 < s.size()) {
            uint32_t cp = ((static_cast<uint32_t>(c0) & 0x0F) << 12) |
                          ((static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1])) & 0x3F) << 6) |
                          (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 2])) & 0x3F);
            pos += 3;
            return cp;
        }

        if ((c0 & 0xF8) == 0xF0 && pos + 3 < s.size()) {
            uint32_t cp = ((static_cast<uint32_t>(c0) & 0x07) << 18) |
                          ((static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 1])) & 0x3F) << 12) |
                          ((static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 2])) & 0x3F) << 6) |
                          (static_cast<uint32_t>(static_cast<unsigned char>(s[pos + 3])) & 0x3F);
            pos += 4;
            return cp;
        }

        ++pos;
        return 0xFFFD;
    }

    static uint32_t findHalfwidthBase(uint32_t cp) {
        static const std::pair<uint32_t, uint32_t> baseMap[] = {
            {0x30A2, 0xFF71}, {0x30A4, 0xFF72}, {0x30A6, 0xFF73}, {0x30A8, 0xFF74}, {0x30AA, 0xFF75},
            {0x30AB, 0xFF76}, {0x30AD, 0xFF77}, {0x30AF, 0xFF78}, {0x30B1, 0xFF79}, {0x30B3, 0xFF7A},
            {0x30B5, 0xFF7B}, {0x30B7, 0xFF7C}, {0x30B9, 0xFF7D}, {0x30BB, 0xFF7E}, {0x30BD, 0xFF7F},
            {0x30BF, 0xFF80}, {0x30C1, 0xFF81}, {0x30C4, 0xFF82}, {0x30C6, 0xFF83}, {0x30C8, 0xFF84},
            {0x30CA, 0xFF85}, {0x30CB, 0xFF86}, {0x30CC, 0xFF87}, {0x30CD, 0xFF88}, {0x30CE, 0xFF89},
            {0x30CF, 0xFF8A}, {0x30D2, 0xFF8B}, {0x30D5, 0xFF8C}, {0x30D8, 0xFF8D}, {0x30DB, 0xFF8E},
            {0x30DE, 0xFF8F}, {0x30DF, 0xFF90}, {0x30E0, 0xFF91}, {0x30E1, 0xFF92}, {0x30E2, 0xFF93},
            {0x30E4, 0xFF94}, {0x30E6, 0xFF95}, {0x30E8, 0xFF96}, {0x30E9, 0xFF97}, {0x30EA, 0xFF98},
            {0x30EB, 0xFF99}, {0x30EC, 0xFF9A}, {0x30ED, 0xFF9B}, {0x30EF, 0xFF9C}, {0x30F2, 0xFF66},
            {0x30F3, 0xFF9D}
        };

        for (const auto& p : baseMap) {
            if (p.first == cp) return p.second;
        }
        return 0;
    }

    static uint32_t findHalfwidthSmall(uint32_t cp) {
        static const std::pair<uint32_t, uint32_t> smallMap[] = {
            {0x30A1, 0xFF67}, {0x30A3, 0xFF68}, {0x30A5, 0xFF69}, {0x30A7, 0xFF6A}, {0x30A9, 0xFF6B},
            {0x30C3, 0xFF6F}, {0x30E3, 0xFF6C}, {0x30E5, 0xFF6D}, {0x30E7, 0xFF6E}
        };

        for (const auto& p : smallMap) {
            if (p.first == cp) return p.second;
        }
        return 0;
    }

    static uint32_t findHalfwidthPunct(uint32_t cp) {
        static const std::pair<uint32_t, uint32_t> punctMap[] = {
            {0x3000, 0x0020},
            {0x3001, 0xFF64},
            {0x3002, 0xFF61},
            {0x300C, 0xFF62},
            {0x300D, 0xFF63},
            {0x30FB, 0xFF65},
            {0x30FC, 0xFF70},
            {0x309B, 0xFF9E},
            {0x309C, 0xFF9F},
            {0x3099, 0xFF9E},
            {0x309A, 0xFF9F}
        };

        for (const auto& p : punctMap) {
            if (p.first == cp) return p.second;
        }
        return 0;
    }

    static std::string toHalfWidthKana(const std::string& s) {
        std::string out;
        size_t pos = 0;

        while (pos < s.size()) {
            uint32_t cp = decodeUtf8(s, pos);

            if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;

            if (uint32_t half = findHalfwidthSmall(cp)) {
                appendUtf8(out, half);
                continue;
            }

            if (uint32_t half = findHalfwidthBase(cp)) {
                appendUtf8(out, half);
                continue;
            }

            if (cp == 0x30F4) {
                appendUtf8(out, 0xFF73);
                appendUtf8(out, 0xFF9E);
                continue;
            }

            if (cp >= 0x30AC && cp <= 0x30DD) {
                if (uint32_t base = findHalfwidthBase(cp - 1)) {
                    appendUtf8(out, base);
                    appendUtf8(out, 0xFF9E);
                    continue;
                }
                if (uint32_t base = findHalfwidthBase(cp - 2)) {
                    appendUtf8(out, base);
                    appendUtf8(out, 0xFF9F);
                    continue;
                }
            }

            if (uint32_t punct = findHalfwidthPunct(cp)) {
                appendUtf8(out, punct);
                continue;
            }

            appendUtf8(out, cp);
        }

        return out;
    }

    static std::string kanaToRomaji(std::string_view hira) {
        static const std::pair<std::string_view, std::string_view> kTable[] = {
            {"きゃ","kya"},{"きゅ","kyu"},{"きょ","kyo"},
            {"ぎゃ","gya"},{"ぎゅ","gyu"},{"ぎょ","gyo"},
            {"しゃ","sha"},{"しゅ","shu"},{"しょ","sho"},{"しぇ","she"},
            {"じゃ","ja"}, {"じゅ","ju"}, {"じょ","jo"}, {"じぇ","je"},
            {"ちゃ","cha"},{"ちゅ","chu"},{"ちょ","cho"},{"ちぇ","che"},
            {"ぢゃ","dya"},{"ぢゅ","dyu"},{"ぢょ","dyo"},
            {"にゃ","nya"},{"にゅ","nyu"},{"にょ","nyo"},
            {"ひゃ","hya"},{"ひゅ","hyu"},{"ひょ","hyo"},
            {"びゃ","bya"},{"びゅ","byu"},{"びょ","byo"},
            {"ぴゃ","pya"},{"ぴゅ","pyu"},{"ぴょ","pyo"},
            {"みゃ","mya"},{"みゅ","myu"},{"みょ","myo"},
            {"りゃ","rya"},{"りゅ","ryu"},{"りょ","ryo"},
            {"ふぁ","fa"}, {"ふぃ","fi"}, {"ふぇ","fe"}, {"ふぉ","fo"},
            {"てぃ","thi"},{"でぃ","dhi"},{"とぅ","twu"},{"どぅ","dwu"},
            {"うぃ","wi"}, {"うぇ","we"},
            {"あ","a"},{"い","i"},{"う","u"},{"え","e"},{"お","o"},
            {"か","ka"},{"き","ki"},{"く","ku"},{"け","ke"},{"こ","ko"},
            {"が","ga"},{"ぎ","gi"},{"ぐ","gu"},{"げ","ge"},{"ご","go"},
            {"さ","sa"},{"し","shi"},{"す","su"},{"せ","se"},{"そ","so"},
            {"ざ","za"},{"じ","ji"},{"ず","zu"},{"ぜ","ze"},{"ぞ","zo"},
            {"た","ta"},{"ち","chi"},{"つ","tsu"},{"て","te"},{"と","to"},
            {"だ","da"},{"ぢ","di"},{"づ","du"},{"で","de"},{"ど","do"},
            {"な","na"},{"に","ni"},{"ぬ","nu"},{"ね","ne"},{"の","no"},
            {"は","ha"},{"ひ","hi"},{"ふ","fu"},{"へ","he"},{"ほ","ho"},
            {"ば","ba"},{"び","bi"},{"ぶ","bu"},{"べ","be"},{"ぼ","bo"},
            {"ぱ","pa"},{"ぴ","pi"},{"ぷ","pu"},{"ぺ","pe"},{"ぽ","po"},
            {"ま","ma"},{"み","mi"},{"む","mu"},{"め","me"},{"も","mo"},
            {"や","ya"},{"ゆ","yu"},{"よ","yo"},
            {"ら","ra"},{"り","ri"},{"る","ru"},{"れ","re"},{"ろ","ro"},
            {"わ","wa"},{"を","wo"},{"ん","n"},
            {"ぁ","xa"},{"ぃ","xi"},{"ぅ","xu"},{"ぇ","xe"},{"ぉ","xo"},
            {"ゃ","xya"},{"ゅ","xyu"},{"ょ","xyo"},{"ゎ","xwa"},
            {"ー","-"},{"、",","},{"。","."},{"・","/"},{"「","["},{"」","]"}
        };

        std::string out;
        size_t i = 0;
        bool sokuon = false;
        while (i < hira.size()) {
            std::string_view rem = hira.substr(i);
            if (rem.rfind("っ", 0) == 0) {
                sokuon = true;
                i += 3;
                continue;
            }
            bool matched = false;
            for (const auto& [k, r] : kTable) {
                if (rem.rfind(k, 0) == 0) {
                    if (sokuon && !r.empty()) {
                        out.push_back(r[0]);
                        sokuon = false;
                    }
                    out += r;
                    i += k.size();
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                if (sokuon) {
                    out += "xtu";
                    sokuon = false;
                }
                out.push_back(hira[i]);
                i++;
            }
        }
        if (sokuon) out += "xtu";
        return out;
    }

    static std::string applyFnConversion(const std::string& hira, const std::string& rawAscii, fcitx::KeySym sym) {
        if (sym == FcitxKey_F6) return hira;
        if (sym == FcitxKey_F10) return rawAscii.empty() ? hira : rawAscii;
        if (sym == FcitxKey_F8) return toHalfWidthKana(hira);
        if (sym == FcitxKey_F9) {
            const std::string& in = rawAscii.empty() ? hira : rawAscii;
            std::string out;
            size_t pos = 0;

            while (pos < in.size()) {
                uint32_t cp = decodeUtf8(in, pos);

                if (cp == 0x20) {
                    appendUtf8(out, 0x3000);
                } else if (cp >= 0x21 && cp <= 0x7E) {
                    appendUtf8(out, cp + 0xFEE0);
                } else {
                    appendUtf8(out, cp);
                }
            }

            return out;
        }
        if (sym == FcitxKey_F7) {
            std::string out;
            for (size_t i = 0; i < hira.size();) {
                unsigned char c0 = hira[i];
                if (c0 == 0xE3 && i + 2 < hira.size()) {
                    unsigned char c1 = hira[i + 1];
                    unsigned char c2 = hira[i + 2];
                    uint32_t cp = ((c0 & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
                    if (cp >= 0x3041 && cp <= 0x3096) cp += 0x60;
                    out += static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                    i += 3;
                } else {
                    out += hira[i++];
                }
            }
            return out;
        }
        return hira;
    }
};

} // namespace minimal_ime
