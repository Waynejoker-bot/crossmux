#include "AirPageIGrowthCrypto.h"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstring>
#include <iterator>

namespace airpage::crypto {

namespace {

constexpr size_t kSha256BlockSize = 64;

bool validInput(const uint8_t* bytes, const size_t size) { return bytes != nullptr || size == 0; }

bool sha256Parts(const uint8_t* first, const size_t firstSize, const uint8_t* second, const size_t secondSize,
                 uint8_t digest[kSha256DigestSize]) {
  if (!digest || !validInput(first, firstSize) || !validInput(second, secondSize)) return false;
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  if (ok && firstSize > 0) ok = mbedtls_sha256_update(&context, first, firstSize) == 0;
  if (ok && secondSize > 0) ok = mbedtls_sha256_update(&context, second, secondSize) == 0;
  if (ok) ok = mbedtls_sha256_finish(&context, digest) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}

}  // namespace

bool sha256(const uint8_t* bytes, const size_t size, uint8_t digest[kSha256DigestSize]) {
  return sha256Parts(bytes, size, nullptr, 0, digest);
}

bool hmacSha256(const uint8_t* key, const size_t keySize, const uint8_t* message, const size_t messageSize,
                uint8_t digest[kSha256DigestSize]) {
  if (!digest || !validInput(key, keySize) || !validInput(message, messageSize)) return false;

  uint8_t keyBlock[kSha256BlockSize]{};
  if (keySize > kSha256BlockSize) {
    if (!sha256(key, keySize, keyBlock)) return false;
  } else if (keySize > 0) {
    memcpy(keyBlock, key, keySize);
  }

  uint8_t innerPad[kSha256BlockSize];
  uint8_t outerPad[kSha256BlockSize];
  for (size_t index = 0; index < kSha256BlockSize; ++index) {
    innerPad[index] = keyBlock[index] ^ 0x36u;
    outerPad[index] = keyBlock[index] ^ 0x5cu;
  }

  uint8_t innerDigest[kSha256DigestSize];
  bool ok = sha256Parts(innerPad, sizeof(innerPad), message, messageSize, innerDigest) &&
            sha256Parts(outerPad, sizeof(outerPad), innerDigest, sizeof(innerDigest), digest);
  std::fill(std::begin(keyBlock), std::end(keyBlock), 0);
  std::fill(std::begin(innerPad), std::end(innerPad), 0);
  std::fill(std::begin(outerPad), std::end(outerPad), 0);
  std::fill(std::begin(innerDigest), std::end(innerDigest), 0);
  return ok;
}

}  // namespace airpage::crypto
