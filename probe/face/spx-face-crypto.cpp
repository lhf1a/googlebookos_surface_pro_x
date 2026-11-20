// SPDX-License-Identifier: GPL-2.0-only
#include "spx-face-crypto.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

namespace spx::face {
namespace {

inline uint32_t rotr32(uint32_t v, unsigned n) {
    return (v >> n) | (v << (32u - n));
}

inline uint32_t rotl32(uint32_t v, unsigned n) {
    return (v << n) | (v >> (32u - n));
}

inline uint32_t loadBe32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline uint32_t loadLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline void storeBe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>((v >> 24) & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[3] = static_cast<uint8_t>(v & 0xFFu);
}

inline void storeLe32(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

inline void storeLe64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
    }
}

inline void storeBe64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        p[i] = static_cast<uint8_t>((v >> ((7 - i) * 8)) & 0xFFu);
    }
}

struct Sha256Ctx {
    uint32_t state[8]{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    uint64_t totalBits = 0;
    uint8_t block[64]{};
    size_t blockLen = 0;

    void transform(const uint8_t* blk) {
        static constexpr uint32_t k[64]{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) w[i] = loadBe32(blk + i * 4);
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            const uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
        secureZero(w, sizeof(w));
    }

    void update(const uint8_t* data, size_t len) {
        if (!data || len == 0) return;
        totalBits += static_cast<uint64_t>(len) * 8ull;
        size_t pos = 0;
        if (blockLen > 0) {
            const size_t take = std::min(64u - blockLen, len);
            std::memcpy(block + blockLen, data, take);
            blockLen += take;
            pos += take;
            if (blockLen == 64) {
                transform(block);
                blockLen = 0;
            }
        }
        while (pos + 64 <= len) {
            transform(data + pos);
            pos += 64;
        }
        if (pos < len) {
            blockLen = len - pos;
            std::memcpy(block, data + pos, blockLen);
        }
    }

    std::array<uint8_t, 32> finish() {
        block[blockLen++] = 0x80u;
        if (blockLen > 56) {
            std::memset(block + blockLen, 0, 64 - blockLen);
            transform(block);
            blockLen = 0;
        }
        std::memset(block + blockLen, 0, 56 - blockLen);
        storeBe64(block + 56, totalBits);
        transform(block);

        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) storeBe32(out.data() + i * 4, state[i]);
        secureZero(block, sizeof(block));
        return out;
    }
};

inline void quarterRound(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d) {
    a += b; d = rotl32(d ^ a, 16);
    c += d; b = rotl32(b ^ c, 12);
    a += b; d = rotl32(d ^ a, 8);
    c += d; b = rotl32(b ^ c, 7);
}

void chacha20Block(const std::array<uint8_t, 32>& key,
                   uint32_t counter,
                   const std::array<uint8_t, 12>& nonce,
                   uint8_t out[64]) {
    uint32_t s[16]{
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
        loadLe32(key.data() + 0), loadLe32(key.data() + 4),
        loadLe32(key.data() + 8), loadLe32(key.data() + 12),
        loadLe32(key.data() + 16), loadLe32(key.data() + 20),
        loadLe32(key.data() + 24), loadLe32(key.data() + 28),
        counter,
        loadLe32(nonce.data() + 0), loadLe32(nonce.data() + 4), loadLe32(nonce.data() + 8),
    };
    uint32_t x[16];
    std::memcpy(x, s, sizeof(x));
    for (int i = 0; i < 10; ++i) {
        quarterRound(x[0], x[4], x[8], x[12]);
        quarterRound(x[1], x[5], x[9], x[13]);
        quarterRound(x[2], x[6], x[10], x[14]);
        quarterRound(x[3], x[7], x[11], x[15]);
        quarterRound(x[0], x[5], x[10], x[15]);
        quarterRound(x[1], x[6], x[11], x[12]);
        quarterRound(x[2], x[7], x[8], x[13]);
        quarterRound(x[3], x[4], x[9], x[14]);
    }
    for (int i = 0; i < 16; ++i) storeLe32(out + i * 4, x[i] + s[i]);
    secureZero(x, sizeof(x));
    secureZero(s, sizeof(s));
}

}  // namespace

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    Sha256Ctx ctx;
    ctx.update(data, len);
    return ctx.finish();
}

std::array<uint8_t, 32> hmacSha256(const uint8_t* key,
                                   size_t keyLen,
                                   const uint8_t* msg,
                                   size_t msgLen) {
    std::array<uint8_t, 64> keyBlock{};
    if (key && keyLen > 64) {
        auto hashed = sha256(key, keyLen);
        std::memcpy(keyBlock.data(), hashed.data(), 32);
        secureZero(hashed.data(), hashed.size());
    } else if (key && keyLen > 0) {
        std::memcpy(keyBlock.data(), key, keyLen);
    }

    std::array<uint8_t, 64> ipad{};
    std::array<uint8_t, 64> opad{};
    for (size_t i = 0; i < 64; ++i) {
        ipad[i] = keyBlock[i] ^ 0x36u;
        opad[i] = keyBlock[i] ^ 0x5cu;
    }

    Sha256Ctx inner;
    inner.update(ipad.data(), ipad.size());
    inner.update(msg, msgLen);
    auto innerHash = inner.finish();

    Sha256Ctx outer;
    outer.update(opad.data(), opad.size());
    outer.update(innerHash.data(), innerHash.size());
    auto mac = outer.finish();

    secureZero(keyBlock.data(), keyBlock.size());
    secureZero(ipad.data(), ipad.size());
    secureZero(opad.data(), opad.size());
    secureZero(innerHash.data(), innerHash.size());
    return mac;
}

std::array<uint8_t, 32> hkdfSha256(const uint8_t* salt,
                                   size_t saltLen,
                                   const uint8_t* ikm,
                                   size_t ikmLen,
                                   const uint8_t* info,
                                   size_t infoLen) {
    static const uint8_t kZeroSalt[32]{};
    const uint8_t* actualSalt = (salt && saltLen > 0) ? salt : kZeroSalt;
    const size_t actualSaltLen = (salt && saltLen > 0) ? saltLen : sizeof(kZeroSalt);
    auto prk = hmacSha256(actualSalt, actualSaltLen, ikm, ikmLen);

    std::array<uint8_t, 128> t1Input{};
    const size_t safeInfoLen = std::min(infoLen, t1Input.size() - 1u);
    if (info && safeInfoLen > 0) {
        std::memcpy(t1Input.data(), info, safeInfoLen);
    }
    t1Input[safeInfoLen] = 0x01u;
    auto okm = hmacSha256(prk.data(), prk.size(), t1Input.data(), safeInfoLen + 1u);

    secureZero(prk.data(), prk.size());
    secureZero(t1Input.data(), t1Input.size());
    return okm;
}

void chacha20Xor(const std::array<uint8_t, 32>& key,
                 const std::array<uint8_t, 12>& nonce,
                 uint32_t initialCounter,
                 const uint8_t* in,
                 uint8_t* out,
                 size_t len) {
    if ((!in && len > 0) || (!out && len > 0)) return;
    uint8_t block[64]{};
    uint32_t counter = initialCounter;
    size_t pos = 0;
    while (pos < len) {
        chacha20Block(key, counter++, nonce, block);
        const size_t n = std::min<size_t>(64, len - pos);
        for (size_t i = 0; i < n; ++i) {
            out[pos + i] = in[pos + i] ^ block[i];
        }
        pos += n;
    }
    secureZero(block, sizeof(block));
}

bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len) {
    if (!a || !b) return false;
    volatile uint8_t diff = 0;
    for (size_t i = 0; i < len; ++i) {
        diff |= static_cast<uint8_t>(a[i] ^ b[i]);
    }
    return diff == 0;
}

void secureZero(void* ptr, size_t len) {
    if (!ptr || len == 0) return;
    volatile uint8_t* p = static_cast<volatile uint8_t*>(ptr);
    while (len--) *p++ = 0;
}

bool csprngBytes(uint8_t* out, size_t len) {
    if (!out && len > 0) return false;
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t got = 0;
    while (got < len) {
        ssize_t r = read(fd, out + got, len - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        if (r == 0) {
            close(fd);
            return false;
        }
        got += static_cast<size_t>(r);
    }
    close(fd);
    return true;
}

int64_t csprngInt64NonZero() {
    uint64_t v = 0;
    do {
        if (!csprngBytes(reinterpret_cast<uint8_t*>(&v), sizeof(v))) {
            return 1;
        }
    } while (v == 0);
    return static_cast<int64_t>(v);
}

std::array<uint8_t, 37> serializeAuthTokenPayload(int64_t challenge,
                                                  int64_t userId,
                                                  int64_t authenticatorId,
                                                  uint32_t authenticatorType,
                                                  int64_t timestampMs) {
    std::array<uint8_t, 37> payload{};
    payload[0] = 0;  // hw_auth_token_t version
    storeLe64(payload.data() + 1, static_cast<uint64_t>(challenge));
    storeLe64(payload.data() + 9, static_cast<uint64_t>(userId));
    storeLe64(payload.data() + 17, static_cast<uint64_t>(authenticatorId));
    storeBe32(payload.data() + 25, authenticatorType);
    storeBe64(payload.data() + 29, static_cast<uint64_t>(timestampMs));
    return payload;
}

std::array<uint8_t, 32> computeAuthTokenMac(const std::array<uint8_t, 32>& key,
                                            int64_t challenge,
                                            int64_t userId,
                                            int64_t authenticatorId,
                                            uint32_t authenticatorType,
                                            int64_t timestampMs) {
    auto payload = serializeAuthTokenPayload(
        challenge, userId, authenticatorId, authenticatorType, timestampMs);
    return hmacSha256(key.data(), key.size(), payload.data(), payload.size());
}

bool isPlausibleIncomingHat(const uint8_t* mac,
                            size_t macLen,
                            int64_t challenge,
                            const std::set<int64_t>* activeChallenges) {
    if (!mac || macLen != 32) return false;
    bool allZero = true;
    for (size_t i = 0; i < macLen; ++i) {
        if (mac[i] != 0) {
            allZero = false;
            break;
        }
    }
    if (allZero) return false;
    if (activeChallenges != nullptr) {
        return activeChallenges->count(challenge) > 0;
    }
    return challenge != 0;
}

}  // namespace spx::face
