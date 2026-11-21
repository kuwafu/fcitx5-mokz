#pragma once
#include <stdint.h>

namespace hp_ime {

struct alignas(64) RerankerHeader {
    char     magic[8];          // "MYGRULM1"
    uint32_t version;           // 1
    uint32_t vocab_size;        // 5000
    uint32_t embed_dim;         // 64
    uint32_t hidden_dim;        // 128
    uint32_t num_layers;        // 2
    uint32_t char_map_count;    // Number of Unicode codepoints mapped
    uint32_t char_map_offset;   // Offset to CharMapEntry[char_map_count]
    uint32_t weights_offset;    // Offset to 64B-aligned quantized weight blocks
    uint8_t  reserved[24];
};
static_assert(sizeof(RerankerHeader) == 64, "RerankerHeader must be 64 bytes");

struct CharMapEntry {
    uint32_t codepoint;
    uint16_t token_id;
    uint16_t pad;
};
static_assert(sizeof(CharMapEntry) == 8, "CharMapEntry must be 8 bytes");

} // namespace hp_ime
