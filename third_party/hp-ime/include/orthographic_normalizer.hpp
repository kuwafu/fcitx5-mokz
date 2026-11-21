#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hp_ime {

// CAVEAT-04 対応: ベンチマーク評価時の表記ゆれ（アラビア数字⇔漢数字、代表的送り仮名・漢字開き）正規化器
inline std::string normalizeOrthography(std::string_view input) {
    std::string s(input);
    static const std::pair<std::string_view, std::string_view> kRules[] = {
        {"０", "0"}, {"１", "1"}, {"２", "2"}, {"３", "3"}, {"４", "4"},
        {"５", "5"}, {"６", "6"}, {"７", "7"}, {"８", "8"}, {"９", "9"},
        {"一", "1"}, {"二", "2"}, {"三", "3"}, {"四", "4"}, {"五", "5"},
        {"六", "6"}, {"七", "7"}, {"八", "8"}, {"九", "9"},
        {"更に", "さらに"},
        {"殆ど", "ほとんど"},
        {"既に", "すでに"},
        {"全て", "すべて"},
        {"様々", "さまざま"},
        {"出来る", "できる"},
        {"下さい", "ください"},
        {"頂く", "いただく"},
        {"頂き", "いただき"},
        {"致し", "いたし"},
        {"為", "ため"},
        {"事", "こと"},
        {"物", "もの"},
        {"時", "とき"},
        {"所", "ところ"},
        {"子供", "子ども"},
        {"一人", "1人"},
        {"二人", "2人"},
        {"一つ", "1つ"},
        {"二つ", "2つ"},
        {"三つ", "3つ"},
        {"初め", "始め"}
    };

    for (const auto& [from, to] : kRules) {
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    return s;
}

} // namespace hp_ime
