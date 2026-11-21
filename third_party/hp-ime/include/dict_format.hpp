#pragma once
#include <cstdint>

namespace hp_ime {

// 設計書記載の10フィールドをパディング無しで厳密に64バイトに配置
struct alignas(64) DictHeader {
    char     magic[8];            // [0..7]   "MYIMEDIC" (8B)
    uint32_t version;             // [8..11]  Format version (4B)
    uint32_t marisa_size;         // [12..15] MARISA-trie byte size (4B)
    uint64_t marisa_offset;       // [16..23] 64B-aligned offset (8B)
    uint64_t token_table_offset;  // [24..31] 64B-aligned offset (8B)
    uint32_t token_count;         // [32..35] Total WordTokens (4B)
    uint32_t token_stride;        // [36..39] sizeof(WordToken) == 12 (4B)
    uint64_t string_pool_offset;  // [40..47] 64B-aligned offset (8B)
    uint32_t string_pool_size;    // [48..51] String pool byte size (4B)
    uint32_t pos_count;           // [52..55] Mozc POS dimension == 2672 (4B)
    uint64_t pos_matrix_offset;   // [56..63] 64B-aligned offset (8B)
};
static_assert(sizeof(DictHeader) == 64, "DictHeader must be strictly 64 bytes");

#pragma pack(push, 1)
struct WordToken {
    uint32_t string_offset;
    uint16_t left_pos_id;
    uint16_t right_pos_id;
    int16_t  cost;
    uint16_t flags;
};
#pragma pack(pop)
static_assert(sizeof(WordToken) == 12, "WordToken must be strictly 12 bytes");

inline constexpr uint64_t align64(uint64_t val) noexcept {
    return (val + 63ULL) & ~63ULL;
}

} // namespace hp_ime
