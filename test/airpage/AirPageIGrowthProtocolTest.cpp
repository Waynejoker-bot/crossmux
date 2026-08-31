#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include "AirPageIGrowthProtocol.h"

TEST(AirPageIGrowthProtocol, MapsEveryPhysicalActionToTheClosedServerContract) {
  using airpage::igrowth::Button;
  EXPECT_STREQ(airpage::igrowth::actionContract(Button::Back).actionId, "dismiss");
  EXPECT_STREQ(airpage::igrowth::actionContract(Button::Confirm).actionId, "continue");
  EXPECT_STREQ(airpage::igrowth::actionContract(Button::Left).actionId, "explain");
  EXPECT_STREQ(airpage::igrowth::actionContract(Button::Right).actionId, "next");
}

TEST(AirPageIGrowthProtocol, CopiesOneServerFrozenLabelIntoABoundedStaticBuffer) {
  char label[airpage::igrowth::kActionLabelCapacity]{};

  EXPECT_TRUE(airpage::igrowth::copyActionLabel("现在就看", label, sizeof(label)));
  EXPECT_STREQ(label, "现在就看");
  EXPECT_FALSE(airpage::igrowth::copyActionLabel("", label, sizeof(label)));
  EXPECT_FALSE(airpage::igrowth::copyActionLabel("包含\n换行", label, sizeof(label)));

  char tooSmall[4]{};
  EXPECT_FALSE(airpage::igrowth::copyActionLabel("现在", tooSmall, sizeof(tooSmall)));
}

TEST(AirPageIGrowthProtocol, ParsesOnlyTheDeliveryTrailerStampedByIGrowth) {
  std::array<uint8_t, airpage::igrowth::kDeliveryTrailerSize> trailer{};
  constexpr char magic[] = "IGROWTH-AIRPAGE\0v1";
  memcpy(trailer.data(), magic, sizeof(magic));
  trailer[19] = 7;
  uint32_t page = 0;

  EXPECT_TRUE(airpage::igrowth::parseDeliveryTrailer(trailer.data(), trailer.size(), page));
  EXPECT_EQ(page, 7U);

  trailer[0] = 'X';
  EXPECT_FALSE(airpage::igrowth::parseDeliveryTrailer(trailer.data(), trailer.size(), page));
}

TEST(AirPageIGrowthProtocol, CanonicalRequestMatchesTheMessageStationWireContract) {
  char canonical[256];
  ASSERT_TRUE(airpage::igrowth::buildCanonicalRequest(
      "POST", "/msapi/airpage/device/events", 1788100000123ULL,
      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", canonical, sizeof(canonical)));
  EXPECT_STREQ(canonical,
               "airpage-device-request:v1\nPOST\n/msapi/airpage/device/events\n1788100000123\n"
               "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
}
