#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <cctype>

namespace minimal_ime {

class RomajiConverter {
public:
    struct ConvertResult {
        std::string committedKana;
        std::string pendingAscii;
    };

    RomajiConverter() {
        initTable();
    }

    ConvertResult feed(char c) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        pending_ += c;

        std::string committed;

        while (!pending_.empty()) {
            if (auto it = table_.find(pending_); it != table_.end()) {
                committed += it->second;
                pending_.clear();
                break;
            }

            if (pending_.size() >= 2 && pending_[0] == pending_[1] &&
                pending_[0] >= 'a' && pending_[0] <= 'z' &&
                pending_[0] != 'n' && pending_[0] != 'a' && pending_[0] != 'i' &&
                pending_[0] != 'u' && pending_[0] != 'e' && pending_[0] != 'o') {
                committed += "っ";
                pending_ = pending_.substr(1);
                continue;
            }

            if (pending_.size() >= 2 && pending_[0] == 'n' &&
                pending_[1] != 'a' && pending_[1] != 'i' && pending_[1] != 'u' &&
                pending_[1] != 'e' && pending_[1] != 'o' && pending_[1] != 'y' &&
                pending_[1] != 'n') {
                committed += "ん";
                if (pending_[1] == '\'') {
                    pending_ = pending_.substr(2);
                } else {
                    pending_ = pending_.substr(1);
                }
                continue;
            }

            if (isPrefix(pending_)) {
                break;
            }

            committed += pending_[0];
            pending_ = pending_.substr(1);
        }

        return {committed, pending_};
    }

    bool backspace() {
        if (!pending_.empty()) {
            pending_.pop_back();
            return true;
        }
        return false;
    }

    std::string flush() {
        std::string res;
        if (pending_ == "n") {
            res = "ん";
            pending_.clear();
        } else {
            res = pending_;
            pending_.clear();
        }
        return res;
    }

    ConvertResult appendLiteral(char c) {
        std::string committed;
        if (pending_ == "n" && c >= 'A' && c <= 'Z') {
            committed = "ん";
            pending_.clear();
        }
        pending_ += c;
        return {committed, pending_};
    }

    const std::string& pending() const { return pending_; }
    void clear() { pending_.clear(); }

private:
    std::string pending_;
    std::unordered_map<std::string, std::string> table_;

    bool isPrefix(const std::string& s) const {
        for (const auto& [key, _] : table_) {
            if (key.size() > s.size() && key.rfind(s, 0) == 0) {
                return true;
            }
        }
        return false;
    }

    void initTable() {
        table_["a"] = "あ"; table_["i"] = "い"; table_["u"] = "う"; table_["e"] = "え"; table_["o"] = "お";
        table_["ka"] = "か"; table_["ki"] = "き"; table_["ku"] = "く"; table_["ke"] = "け"; table_["ko"] = "こ";
        table_["ga"] = "が"; table_["gi"] = "ぎ"; table_["gu"] = "ぐ"; table_["ge"] = "げ"; table_["go"] = "ご";
        table_["sa"] = "さ"; table_["si"] = "し"; table_["su"] = "す"; table_["se"] = "せ"; table_["so"] = "そ";
        table_["shi"] = "し";
        table_["za"] = "ざ"; table_["zi"] = "じ"; table_["zu"] = "ず"; table_["ze"] = "ぜ"; table_["zo"] = "ぞ";
        table_["ji"] = "じ";
        table_["ta"] = "た"; table_["ti"] = "ち"; table_["tu"] = "つ"; table_["te"] = "て"; table_["to"] = "と";
        table_["chi"] = "ち"; table_["tsu"] = "つ";
        table_["da"] = "だ"; table_["di"] = "ぢ"; table_["du"] = "づ"; table_["de"] = "で"; table_["do"] = "ど";
        table_["na"] = "な"; table_["ni"] = "に"; table_["nu"] = "ぬ"; table_["ne"] = "ね"; table_["no"] = "の";
        table_["nn"] = "ん"; table_["xn"] = "ん";
        table_["ha"] = "は"; table_["hi"] = "ひ"; table_["hu"] = "ふ"; table_["he"] = "へ"; table_["ho"] = "ほ";
        table_["fu"] = "ふ";
        table_["ba"] = "ば"; table_["bi"] = "び"; table_["bu"] = "ぶ"; table_["be"] = "べ"; table_["bo"] = "ぼ";
        table_["pa"] = "ぱ"; table_["pi"] = "ぴ"; table_["pu"] = "ぷ"; table_["pe"] = "ぺ"; table_["po"] = "ぽ";
        table_["ma"] = "ま"; table_["mi"] = "み"; table_["mu"] = "む"; table_["me"] = "め"; table_["mo"] = "も";
        table_["ya"] = "や"; table_["yu"] = "ゆ"; table_["yo"] = "よ";
        table_["ra"] = "ら"; table_["ri"] = "り"; table_["ru"] = "る"; table_["re"] = "れ"; table_["ro"] = "ろ";
        table_["wa"] = "わ"; table_["wi"] = "うぃ"; table_["we"] = "うぇ"; table_["wo"] = "を";

        // 拗音・外来音
        table_["kya"] = "きゃ"; table_["kyu"] = "きゅ"; table_["kyo"] = "きょ";
        table_["gya"] = "ぎゃ"; table_["gyu"] = "ぎゅ"; table_["gyo"] = "ぎょ";
        table_["sya"] = "しゃ"; table_["syu"] = "しゅ"; table_["syo"] = "しょ";
        table_["sha"] = "しゃ"; table_["shu"] = "しゅ"; table_["she"] = "しぇ"; table_["sho"] = "しょ";
        table_["zya"] = "じゃ"; table_["zyu"] = "じゅ"; table_["zyo"] = "じょ";
        table_["ja"] = "じゃ"; table_["ju"] = "じゅ"; table_["je"] = "じぇ"; table_["jo"] = "じょ";
        table_["tya"] = "ちゃ"; table_["tyu"] = "ちゅ"; table_["tyo"] = "ちょ";
        table_["cha"] = "ちゃ"; table_["chu"] = "ちゅ"; table_["che"] = "ちぇ"; table_["cho"] = "ちょ";
        table_["dya"] = "ぢゃ"; table_["dyu"] = "ぢゅ"; table_["dyo"] = "ぢょ";
        table_["nya"] = "にゃ"; table_["nyu"] = "にゅ"; table_["nyo"] = "にょ";
        table_["hya"] = "ひゃ"; table_["hyu"] = "ひゅ"; table_["hyo"] = "ひょ";
        table_["bya"] = "びゃ"; table_["byu"] = "びゅ"; table_["byo"] = "びょ";
        table_["pya"] = "ぴゃ"; table_["pyu"] = "ぴゅ"; table_["pyo"] = "ぴょ";
        table_["mya"] = "みゃ"; table_["myu"] = "みゅ"; table_["myo"] = "みょ";
        table_["rya"] = "りゃ"; table_["ryu"] = "りゅ"; table_["ryo"] = "りょ";
        table_["fa"] = "ふぁ"; table_["fi"] = "ふぃ"; table_["fe"] = "ふぇ"; table_["fo"] = "ふぉ";
        table_["va"] = "ゔぁ"; table_["vi"] = "ゔぃ"; table_["vu"] = "ゔ"; table_["ve"] = "ゔぇ"; table_["vo"] = "ゔぉ";
        table_["thi"] = "てぃ"; table_["dhi"] = "でぃ"; table_["twu"] = "とぅ"; table_["dwu"] = "どぅ";

        // 小書き文字
        table_["xa"] = "ぁ"; table_["xi"] = "ぃ"; table_["xu"] = "ぅ"; table_["xe"] = "ぇ"; table_["xo"] = "ぉ";
        table_["la"] = "ぁ"; table_["li"] = "ぃ"; table_["lu"] = "ぅ"; table_["le"] = "ぇ"; table_["lo"] = "ぉ";
        table_["xtsu"] = "っ"; table_["ltsu"] = "っ"; table_["xtu"] = "っ"; table_["ltu"] = "っ";
        table_["xya"] = "ゃ"; table_["xyu"] = "ゅ"; table_["xyo"] = "ょ";
        table_["lya"] = "ゃ"; table_["lyu"] = "ゅ"; table_["lyo"] = "ょ";
        table_["xwa"] = "ゎ"; table_["lwa"] = "ゎ";
        table_["n'"] = "ん";

        // 標準拡張: jy / ts / fy / th / dh / q / sy-ty-cy-zy-vy / wh / gw-kw / tch
        table_["jya"] = "じゃ"; table_["jyi"] = "じぃ"; table_["jyu"] = "じゅ"; table_["jye"] = "じぇ"; table_["jyo"] = "じょ";
        table_["tsa"] = "つぁ"; table_["tsi"] = "つぃ"; table_["tse"] = "つぇ"; table_["tso"] = "つぉ";
        table_["fya"] = "ふゃ"; table_["fyu"] = "ふゅ"; table_["fyo"] = "ふょ";
        table_["tha"] = "てゃ"; table_["thi"] = "てぃ"; table_["thu"] = "てゅ"; table_["the"] = "てぇ"; table_["tho"] = "てょ";
        table_["dha"] = "でゃ"; table_["dhi"] = "でぃ"; table_["dhu"] = "でゅ"; table_["dhe"] = "でぇ"; table_["dho"] = "でょ";
        table_["qa"] = "くぁ"; table_["qi"] = "くぃ"; table_["qu"] = "く"; table_["qe"] = "くぇ"; table_["qo"] = "くぉ";
        table_["sye"] = "しぇ"; table_["zye"] = "じぇ"; table_["tye"] = "ちぇ"; table_["dye"] = "ぢぇ";
        table_["cya"] = "ちゃ"; table_["cyu"] = "ちゅ"; table_["cye"] = "ちぇ"; table_["cyo"] = "ちょ";
        table_["vya"] = "ゔゃ"; table_["vyu"] = "ゔゅ"; table_["vyo"] = "ゔょ";
        table_["wha"] = "うぁ"; table_["whi"] = "うぃ"; table_["whu"] = "う"; table_["whe"] = "うぇ"; table_["who"] = "うぉ";
        table_["gwa"] = "ぐぁ"; table_["gwi"] = "ぐぃ"; table_["gwu"] = "ぐ"; table_["gwe"] = "ぐぇ"; table_["gwo"] = "ぐぉ";
        table_["kwa"] = "くぁ"; table_["kwi"] = "くぃ"; table_["kwu"] = "く"; table_["kwe"] = "くぇ"; table_["kwo"] = "くぉ";
        table_["tcha"] = "っちゃ"; table_["tchi"] = "っち"; table_["tchu"] = "っちゅ"; table_["tche"] = "っちぇ"; table_["tcho"] = "っちょ";

        // 記号
        table_["-"] = "ー"; table_[","] = "、"; table_["."] = "。";
        table_["["] = "「"; table_["]"] = "」"; table_["/"] = "・";
        table_["!"] = "！"; table_["?"] = "？"; table_["~"] = "〜";
    }
};

} // namespace minimal_ime
