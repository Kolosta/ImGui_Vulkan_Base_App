#pragma once

// ─────────────────────────────────────────────────────────────────────────────
//  Tiny self-contained PNG writer (no external dependency).
//
//  Encodes an RGBA8 image to an in-memory PNG byte vector. Uses zlib's STORED
//  (uncompressed) deflate blocks — valid PNG that any decoder reads, at the cost
//  of a larger file. That is fine for our use (small .acu thumbnails). Avoids
//  pulling in stb_image_write / zlib just for thumbnails.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cstdint>
#include <vector>

namespace App {
namespace png {

inline uint32_t Crc32(const uint8_t* p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t table[256]; static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

inline void Chunk(std::vector<uint8_t>& out, const char tag[4],
                  const std::vector<uint8_t>& data) {
    PutBE32(out, (uint32_t)data.size());
    size_t crcStart = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = Crc32(out.data() + crcStart, out.size() - crcStart) ^ 0xFFFFFFFFu;
    PutBE32(out, crc);
}

// Adler-32 over the raw (uncompressed) zlib stream.
inline uint32_t Adler32(const uint8_t* p, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) { a = (a + p[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

// Encode RGBA8 (w*h*4, top-left origin) → PNG bytes. Returns false on bad input.
inline bool EncodeRGBA(const uint8_t* rgba, int w, int h, std::vector<uint8_t>& out) {
    if (!rgba || w <= 0 || h <= 0) return false;
    out.clear();
    const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    out.insert(out.end(), sig, sig + 8);

    // IHDR: width, height, bit depth 8, colour type 6 (RGBA), no interlace.
    std::vector<uint8_t> ihdr;
    PutBE32(ihdr, (uint32_t)w); PutBE32(ihdr, (uint32_t)h);
    ihdr.push_back(8); ihdr.push_back(6); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(out, "IHDR", ihdr);

    // Raw image data with a filter byte (0 = none) per row.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * (1 + (size_t)w * 4));
    for (int y = 0; y < h; ++y) {
        raw.push_back(0);
        const uint8_t* row = rgba + (size_t)y * w * 4;
        raw.insert(raw.end(), row, row + (size_t)w * 4);
    }

    // zlib stream: 2-byte header + STORED deflate blocks + Adler-32.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);          // zlib header (no compression)
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t block = std::min<size_t>(65535, raw.size() - pos);
        bool last = (pos + block >= raw.size());
        z.push_back(last ? 1 : 0);                 // BFINAL, BTYPE=00 (stored)
        z.push_back((uint8_t)(block & 0xFF)); z.push_back((uint8_t)(block >> 8));
        uint16_t nlen = (uint16_t)~block;
        z.push_back((uint8_t)(nlen & 0xFF)); z.push_back((uint8_t)(nlen >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + block);
        pos += block;
    }
    uint32_t adler = Adler32(raw.data(), raw.size());
    PutBE32(z, adler);
    Chunk(out, "IDAT", z);

    Chunk(out, "IEND", {});
    return true;
}

} // namespace png
} // namespace App
