#pragma once

#include <cstddef>
#include <cstdint>

namespace airpage::crypto {

constexpr size_t kSha256DigestSize = 32;

bool sha256(const uint8_t* bytes, size_t size, uint8_t digest[kSha256DigestSize]);
bool hmacSha256(const uint8_t* key, size_t keySize, const uint8_t* message, size_t messageSize,
                uint8_t digest[kSha256DigestSize]);

}  // namespace airpage::crypto
