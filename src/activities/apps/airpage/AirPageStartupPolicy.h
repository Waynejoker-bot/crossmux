#pragma once

#include <cstdint>

namespace airpage::startup {

enum class ResumeKind : uint8_t {
  ColdBoot,
  DeepSleepWake,
  SilentRestart,
};

enum class BootLanding : uint8_t {
  FirmwareRecovery,
  CrashReport,
  Onboarding,
  AirPage,
  PreserveSilentDestination,
};

struct BootContext {
  bool recoveryMode = false;
  bool panicRestart = false;
  bool onboardingRequired = false;
  bool postOta = false;
  ResumeKind resume = ResumeKind::ColdBoot;
};

constexpr BootLanding chooseBootLanding(const BootContext& context) {
  if (context.recoveryMode) return BootLanding::FirmwareRecovery;
  if (context.panicRestart) return BootLanding::CrashReport;
  if (context.onboardingRequired) return BootLanding::Onboarding;
  if (context.postOta || context.resume != ResumeKind::SilentRestart) return BootLanding::AirPage;
  return BootLanding::PreserveSilentDestination;
}

enum class AirPageEntryScreen : uint8_t {
  Qr,
  CurrentImage,
};

constexpr AirPageEntryScreen chooseAirPageEntryScreen(const bool hasCurrentImage) {
  return hasCurrentImage ? AirPageEntryScreen::CurrentImage : AirPageEntryScreen::Qr;
}

}  // namespace airpage::startup
