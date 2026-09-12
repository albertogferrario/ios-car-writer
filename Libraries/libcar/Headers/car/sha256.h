/*
 * Minimal, self-contained SHA-256 (FIPS 180-4), header-only.
 *
 * Added in Phase 243 Plan 02 to shape the round-trip tool's structural
 * pre-gate as per-rendition SHA-256 (D-13), so it seeds Phase 244's
 * CatalogVerifier. Used only for content-identity comparison between a
 * source and a round-tripped catalog -- not a cryptographic security
 * boundary, no key material, no secrets involved (243-RESEARCH.md Security
 * Domain, ASVS V6: not applicable).
 *
 * Deliberately vendored rather than depending on a system crypto library
 * (e.g. OpenSSL): the fork's only real build dependency is zlib
 * (243-RESEARCH.md "Delivery"), and this keeps that true for the static-musl
 * build target too.
 */

#ifndef _LIBCAR_SHA256_H
#define _LIBCAR_SHA256_H

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace car {

namespace sha256_detail {

inline uint32_t RotR(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

struct State {
    uint32_t h[8];
    uint8_t buffer[64];
    size_t bufferLength;
    uint64_t totalLength;
};

inline void Init(State &state)
{
    state.h[0] = 0x6a09e667; state.h[1] = 0xbb67ae85;
    state.h[2] = 0x3c6ef372; state.h[3] = 0xa54ff53a;
    state.h[4] = 0x510e527f; state.h[5] = 0x9b05688c;
    state.h[6] = 0x1f83d9ab; state.h[7] = 0x5be0cd19;
    state.bufferLength = 0;
    state.totalLength = 0;
}

inline void ProcessBlock(State &state, uint8_t const *block)
{
    static uint32_t const k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };

    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
               (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(block[i * 4 + 3]));
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = RotR(w[i - 15], 7) ^ RotR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = RotR(w[i - 2], 17) ^ RotR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    uint32_t a = state.h[0], b = state.h[1], c = state.h[2], d = state.h[3];
    uint32_t e = state.h[4], f = state.h[5], g = state.h[6], hh = state.h[7];

    for (int i = 0; i < 64; i++) {
        uint32_t s1 = RotR(e, 6) ^ RotR(e, 11) ^ RotR(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t temp1 = hh + s1 + ch + k[i] + w[i];
        uint32_t s0 = RotR(a, 2) ^ RotR(a, 13) ^ RotR(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        hh = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }

    state.h[0] += a; state.h[1] += b; state.h[2] += c; state.h[3] += d;
    state.h[4] += e; state.h[5] += f; state.h[6] += g; state.h[7] += hh;
}

inline void Update(State &state, void const *data, size_t length)
{
    uint8_t const *bytes = reinterpret_cast<uint8_t const *>(data);
    state.totalLength += length;

    while (length > 0) {
        size_t take = 64 - state.bufferLength;
        if (take > length) {
            take = length;
        }
        memcpy(state.buffer + state.bufferLength, bytes, take);
        state.bufferLength += take;
        bytes += take;
        length -= take;

        if (state.bufferLength == 64) {
            ProcessBlock(state, state.buffer);
            state.bufferLength = 0;
        }
    }
}

inline void Final(State &state, uint8_t digest[32])
{
    uint64_t bitLength = state.totalLength * 8;

    uint8_t pad = 0x80;
    Update(state, &pad, 1);

    uint8_t zero = 0x00;
    while (state.bufferLength != 56) {
        Update(state, &zero, 1);
    }

    uint8_t lengthBytes[8];
    for (int i = 0; i < 8; i++) {
        lengthBytes[i] = static_cast<uint8_t>(bitLength >> (56 - i * 8));
    }
    /* Append length directly (bypassing Update's total-length accumulation,
     * which must reflect only the original message, already captured above). */
    memcpy(state.buffer + state.bufferLength, lengthBytes, 8);
    state.bufferLength += 8;
    ProcessBlock(state, state.buffer);
    state.bufferLength = 0;

    for (int i = 0; i < 8; i++) {
        digest[i * 4] = static_cast<uint8_t>(state.h[i] >> 24);
        digest[i * 4 + 1] = static_cast<uint8_t>(state.h[i] >> 16);
        digest[i * 4 + 2] = static_cast<uint8_t>(state.h[i] >> 8);
        digest[i * 4 + 3] = static_cast<uint8_t>(state.h[i]);
    }
}

} // namespace sha256_detail

inline std::string bytesToHex(void const *data, size_t length)
{
    static char const *hexDigits = "0123456789abcdef";
    uint8_t const *bytes = reinterpret_cast<uint8_t const *>(data);
    std::string result;
    result.reserve(length * 2);
    for (size_t i = 0; i < length; i++) {
        result.push_back(hexDigits[(bytes[i] >> 4) & 0xF]);
        result.push_back(hexDigits[bytes[i] & 0xF]);
    }
    return result;
}

inline std::string sha256Hex(void const *data, size_t length)
{
    sha256_detail::State state;
    sha256_detail::Init(state);
    sha256_detail::Update(state, data, length);

    uint8_t digest[32];
    sha256_detail::Final(state, digest);

    return bytesToHex(digest, sizeof(digest));
}

} // namespace car

#endif /* _LIBCAR_SHA256_H */
