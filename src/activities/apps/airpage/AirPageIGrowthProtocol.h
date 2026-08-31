#pragma once

#include <cstddef>
#include <cstdint>

namespace airpage::igrowth {

enum class Button : uint8_t { Back, Confirm, Left, Right };

struct ActionContract {
  const char* button;
  const char* actionId;
};

constexpr size_t kSha256HexLength = 64;
constexpr size_t kDeliveryTrailerSize = 55;
constexpr size_t kActionLabelCapacity = 49;

const ActionContract& actionContract(Button button);
bool copyActionLabel(const char* value, char* output, size_t outputSize);
bool parseDeliveryTrailer(const uint8_t* trailer, size_t size, uint32_t& pageNumber);
bool buildCanonicalRequest(const char* method, const char* path, uint64_t timestampMs, const char* bodySha256,
                           char* output, size_t outputSize);

}  // namespace airpage::igrowth
