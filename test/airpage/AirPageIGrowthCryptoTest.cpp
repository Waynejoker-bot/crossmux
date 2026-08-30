#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include "AirPageIGrowthCrypto.h"

namespace {

std::string toHex(const uint8_t* bytes, const size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output(size * 2, '\0');
  for (size_t index = 0; index < size; ++index) {
    output[index * 2] = kHex[bytes[index] >> 4u];
    output[index * 2 + 1] = kHex[bytes[index] & 0x0fu];
  }
  return output;
}

TEST(AirPageIGrowthCryptoTest, ComputesSha256KnownVector) {
  constexpr char kMessage[] = "abc";
  std::array<uint8_t, airpage::crypto::kSha256DigestSize> digest{};

  ASSERT_TRUE(airpage::crypto::sha256(reinterpret_cast<const uint8_t*>(kMessage), sizeof(kMessage) - 1, digest.data()));
  EXPECT_EQ(toHex(digest.data(), digest.size()), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(AirPageIGrowthCryptoTest, ComputesRfc4231HmacSha256Vector) {
  constexpr char kMessage[] = "Hi There";
  std::array<uint8_t, 20> key{};
  key.fill(0x0b);
  std::array<uint8_t, airpage::crypto::kSha256DigestSize> digest{};

  ASSERT_TRUE(airpage::crypto::hmacSha256(key.data(), key.size(), reinterpret_cast<const uint8_t*>(kMessage),
                                          sizeof(kMessage) - 1, digest.data()));
  EXPECT_EQ(toHex(digest.data(), digest.size()), "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

}  // namespace
