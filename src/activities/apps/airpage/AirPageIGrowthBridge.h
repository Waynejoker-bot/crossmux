#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "AirPageIGrowthProtocol.h"
#include "AirPageIGrowthService.h"

namespace airpage {

class AirPageIGrowthBridge final {
 public:
  enum class PairingResult : uint8_t { Started, Pending, Paired, NetworkFailed, Invalid };
  enum class ManifestResult : uint8_t { Ready, NotIGrowth, NotPaired, Unavailable };
  enum class ActionResult : uint8_t { Accepted, QueuedOffline, Unavailable };

  bool begin(const char* deviceId, igrowth::ServiceEnvironment environment, const std::string& developerOrigin);
  bool paired() const { return secret_[0] != '\0' && bindingRevision_[0] != '\0'; }
  bool pairingPending() const { return pairingId_[0] != '\0' && claimToken_[0] != '\0'; }
  const char* pairingCode() const { return displayCode_; }

  PairingResult startPairing();
  PairingResult claimPairing();
  ManifestResult loadManifest(const char* imagePath, bool networkAvailable);
  ActionResult sendAction(igrowth::Button button, bool networkAvailable);
  bool drainOneQueuedEvent();
  void clearManifest();
  bool hasManifest() const { return manifestReady_; }
  const char* actionLabel(igrowth::Button button) const;

 private:
  static constexpr size_t kDeviceIdCapacity = 17;
  static constexpr size_t kBindingCapacity = 65;
  static constexpr size_t kSecretCapacity = 65;
  static constexpr size_t kPairingIdCapacity = 41;
  static constexpr size_t kClaimTokenCapacity = 129;
  static constexpr size_t kDisplayCodeCapacity = 9;
  static constexpr size_t kDeliveryIdCapacity = 65;
  static constexpr size_t kShaCapacity = 65;

  bool loadCredential();
  bool saveCredential() const;
  bool loadCachedManifest(const char* imageSha256, uint32_t pageNumber);
  bool saveCachedManifest() const;
  uint64_t loadSequence() const;
  bool saveSequence(uint64_t sequence) const;
  bool inspectIGrowthImage(const char* imagePath, char* imageSha256, size_t imageSha256Size,
                           uint32_t& trailerPage) const;
  bool postJson(const char* path, const std::string& body, bool signedRequest, int& status,
                std::string& response) const;
  bool saveQueuedEvent(uint64_t sequence, const std::string& body) const;
  bool findOldestQueuedEvent(char* path, size_t pathSize, size_t* count = nullptr) const;
  int postEvent(const std::string& body) const;

  char deviceId_[kDeviceIdCapacity]{};
  char bindingRevision_[kBindingCapacity]{};
  char secret_[kSecretCapacity]{};
  char pairingId_[kPairingIdCapacity]{};
  char claimToken_[kClaimTokenCapacity]{};
  char displayCode_[kDisplayCodeCapacity]{};
  char deliveryId_[kDeliveryIdCapacity]{};
  char imageSha256_[kShaCapacity]{};
  char actionLabels_[4][igrowth::kActionLabelCapacity]{};
  uint64_t sequence_ = 0;
  uint64_t pairingExpiresAtMs_ = 0;
  uint32_t pageNumber_ = 0;
  bool manifestReady_ = false;
  igrowth::ServiceEndpoint endpoint_{};
};

}  // namespace airpage
