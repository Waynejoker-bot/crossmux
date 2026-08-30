#include "AirPageIGrowthProtocol.h"

#include <cstdio>
#include <cstring>

namespace airpage::igrowth {

namespace {

constexpr uint8_t kTrailerMagic[] = {'I', 'G', 'R', 'O', 'W', 'T', 'H', '-', 'A', 'I',
                                     'R', 'P', 'A', 'G', 'E', 0,   'v', '1', 0};
constexpr ActionContract kActions[] = {
    {"back", "dismiss"},
    {"confirm", "continue"},
    {"left", "explain"},
    {"right", "next"},
};

bool isLowerHex(const char* value) {
  if (!value || strlen(value) != kSha256HexLength) return false;
  for (size_t i = 0; i < kSha256HexLength; ++i) {
    const char c = value[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

}  // namespace

const ActionContract& actionContract(const Button button) { return kActions[static_cast<uint8_t>(button)]; }

bool parseDeliveryTrailer(const uint8_t* trailer, const size_t size, uint32_t& pageNumber) {
  pageNumber = 0;
  if (!trailer || size != kDeliveryTrailerSize || memcmp(trailer, kTrailerMagic, sizeof(kTrailerMagic)) != 0) {
    return false;
  }
  // The server writes this field little-endian. Decode bytewise so the read is
  // safe even when the trailer begins at an unaligned SD offset on ESP32-C3.
  const size_t offset = sizeof(kTrailerMagic);
  pageNumber = static_cast<uint32_t>(trailer[offset]) | (static_cast<uint32_t>(trailer[offset + 1]) << 8u) |
               (static_cast<uint32_t>(trailer[offset + 2]) << 16u) |
               (static_cast<uint32_t>(trailer[offset + 3]) << 24u);
  return pageNumber > 0;
}

bool buildCanonicalRequest(const char* method, const char* path, const uint64_t timestampMs, const char* bodySha256,
                           char* output, const size_t outputSize) {
  if (!method || strcmp(method, "POST") != 0 || !path || path[0] != '/' || timestampMs == 0 ||
      !isLowerHex(bodySha256) || !output || outputSize == 0) {
    return false;
  }
  const int written = snprintf(output, outputSize, "airpage-device-request:v1\n%s\n%s\n%llu\n%s", method, path,
                               static_cast<unsigned long long>(timestampMs), bodySha256);
  return written > 0 && static_cast<size_t>(written) < outputSize;
}

}  // namespace airpage::igrowth
