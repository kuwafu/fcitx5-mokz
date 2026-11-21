#pragma once
#include <cstdint>
#include <string_view>
#include <algorithm>

namespace hp_ime {

// FNV-1a 32-bit Hash (高速単語ID化・文字列比較ゼロ化)
inline constexpr uint32_t hashWord(std::string_view sv) noexcept {
    uint32_t h = 2166136261u;
    for (unsigned char c : sv) {
        h ^= c;
        h *= 16777619u;
    }
    return h == 0 ? 1 : h;
}

// 64バイト厳密アライメント済み 4-gram バイナリヘッダ
struct alignas(64) KenLM4Header {
    char     magic[8];       // [0..7]   "MYKENLM4" (8B)
    uint32_t version;        // [8..11]  1 (4B)
    uint32_t max_order;      // [12..15] 4 (4B)
    uint32_t counts[4];      // [16..31] 1-gram .. 4-gram entry counts (16B)
    uint64_t offsets[4];     // [32..63] 64B-aligned table offsets (32B)
};
static_assert(sizeof(KenLM4Header) == 64, "KenLM4Header must be strictly 64 bytes");

// 8-bit 量子化コードブック (Header直後の offset=64 に配置)
struct alignas(64) QuantCodebook {
    float prob_table[4][256]; // 各次数の対数確率 8-bit パレット (-q 8)
    float bo_table[3][256];   // 1..3-gram のバックオフ重み 8-bit パレット (-b 8)
    char  padding[64 - ((7 * 256 * sizeof(float)) % 64 == 0 ? 64 : (7 * 256 * sizeof(float)) % 64)];
};

#pragma pack(push, 1)
struct UnigramEntry {
    uint32_t w1;
    uint8_t  prob_q8;
    uint8_t  bo_q8;
    uint16_t pad;
};
static_assert(sizeof(UnigramEntry) == 8);

struct BigramEntry {
    uint64_t key; // (uint64_t(w1) << 32) | w2
    uint8_t  prob_q8;
    uint8_t  bo_q8;
    uint16_t pad;
};
static_assert(sizeof(BigramEntry) == 12);

struct TrigramEntry {
    uint64_t key_hi; // (uint64_t(w1) << 32) | w2
    uint32_t w3;
    uint8_t  prob_q8;
    uint8_t  bo_q8;
    uint16_t pad;
};
static_assert(sizeof(TrigramEntry) == 16);

struct FourgramEntry {
    uint64_t key_hi; // (uint64_t(w1) << 32) | w2
    uint64_t key_lo; // (uint64_t(w3) << 32) | w4
    uint8_t  prob_q8;
    uint8_t  pad[7];
};
static_assert(sizeof(FourgramEntry) == 24);
#pragma pack(pop)

// ハイブリッド方式（マージン足切り＋バックオフ深さペナルティ）の評価結果
struct HybridLMScore {
    float   log_prob;        // Kneser-Ney log10 P(w4 | w1, w2, w3)
    uint8_t matched_order;   // 実際にヒットした次数 (4, 3, 2, 1, 0=UNK)
    int32_t cost_adjustment; // Stage 1 合成コストへの加減算値 (スコア換算 + バックオフ罰則)
};

} // namespace hp_ime
