// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <set>

namespace spx::face {

inline constexpr uint32_t kAuthTypeBiometric = 2u;  // keymaster HardwareAuthenticatorType::FINGERPRINT

std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len);
std::array<uint8_t, 32> hmacSha256(const uint8_t* key,
                                   size_t keyLen,
                                   const uint8_t* msg,
                                   size_t msgLen);

std::array<uint8_t, 32> hkdfSha256(const uint8_t* salt,
                                   size_t saltLen,
                                   const uint8_t* ikm,
                                   size_t ikmLen,
                                   const uint8_t* info,
                                   size_t infoLen);

void chacha20Xor(const std::array<uint8_t, 32>& key,
                 const std::array<uint8_t, 12>& nonce,
                 uint32_t initialCounter,
                 const uint8_t* in,
                 uint8_t* out,
                 size_t len);

bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t len);
void secureZero(void* ptr, size_t len);
bool csprngBytes(uint8_t* out, size_t len);
int64_t csprngInt64NonZero();

// Matches AOSP hw_auth_token_t wire layout (37 bytes):
// [0] version=0, [1..8] challenge LE, [9..16] userId LE,
// [17..24] authenticatorId LE, [25..28] authenticatorType BE, [29..36] timestampMs BE.
std::array<uint8_t, 37> serializeAuthTokenPayload(int64_t challenge,
                                                  int64_t userId,
                                                  int64_t authenticatorId,
                                                  uint32_t authenticatorType,
                                                  int64_t timestampMs);

std::array<uint8_t, 32> computeAuthTokenMac(const std::array<uint8_t, 32>& key,
                                            int64_t challenge,
                                            int64_t userId,
                                            int64_t authenticatorId,
                                            uint32_t authenticatorType,
                                            int64_t timestampMs);

bool isPlausibleIncomingHat(const uint8_t* mac,
                            size_t macLen,
                            int64_t challenge,
                            const std::set<int64_t>* activeChallenges);

}  // namespace spx::face
