#include <gtest/gtest.h>

#include "AirPageStartupPolicy.h"

namespace {

using airpage::startup::AirPageEntryScreen;
using airpage::startup::BootContext;
using airpage::startup::BootLanding;
using airpage::startup::ResumeKind;

TEST(AirPageStartupPolicyTest, ColdBootLandsInAirPage) {
  BootContext context{};
  context.resume = ResumeKind::ColdBoot;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::AirPage);
}

TEST(AirPageStartupPolicyTest, DeepSleepWakeLandsInAirPage) {
  BootContext context{};
  context.resume = ResumeKind::DeepSleepWake;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::AirPage);
}

TEST(AirPageStartupPolicyTest, PostOtaBootLandsInAirPage) {
  BootContext context{};
  context.postOta = true;
  context.resume = ResumeKind::ColdBoot;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::AirPage);
}

TEST(AirPageStartupPolicyTest, RecoveryModeKeepsFirmwareRecovery) {
  BootContext context{};
  context.recoveryMode = true;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::FirmwareRecovery);
}

TEST(AirPageStartupPolicyTest, PanicRestartKeepsCrashReport) {
  BootContext context{};
  context.panicRestart = true;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::CrashReport);
}

TEST(AirPageStartupPolicyTest, OnboardingStillRunsBeforeAirPage) {
  BootContext context{};
  context.onboardingRequired = true;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::Onboarding);
}

TEST(AirPageStartupPolicyTest, MaintenanceRestartKeepsItsExplicitDestination) {
  BootContext context{};
  context.resume = ResumeKind::SilentRestart;

  EXPECT_EQ(airpage::startup::chooseBootLanding(context), BootLanding::PreserveSilentDestination);
}

TEST(AirPageStartupPolicyTest, CachedImageReopensWhenAirPageStarts) {
  EXPECT_EQ(airpage::startup::chooseAirPageEntryScreen(true), AirPageEntryScreen::CurrentImage);
}

TEST(AirPageStartupPolicyTest, EmptyAirPageStartsOnPairingQr) {
  EXPECT_EQ(airpage::startup::chooseAirPageEntryScreen(false), AirPageEntryScreen::Qr);
}

}  // namespace
