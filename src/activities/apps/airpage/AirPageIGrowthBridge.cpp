#include "AirPageIGrowthBridge.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ObfuscationUtils.h>
#include <SecureHttpClient.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <utility>

#include "AirPageIGrowthCrypto.h"

namespace airpage {

namespace {

constexpr char kPairingPath[] = "/msapi/airpage/device/pairings";
constexpr char kClaimPath[] = "/msapi/airpage/device/pairings/claim";
constexpr char kManifestPath[] = "/msapi/airpage/device/manifest";
constexpr char kEventPath[] = "/msapi/airpage/device/events";
constexpr size_t kMaxResponseBytes = 2048;
constexpr size_t kMaxEventBytes = 768;
constexpr size_t kMaxOutboxEntries = 8;
constexpr size_t kMaxIGrowthBmpBytes = 512 * 1024;
constexpr uint64_t kMinimumValidEpochMs = 1704067200000ULL;

// DigiCert Global Root G2. The current igrowth.cc chain terminates at this
// stable root; pinning the root keeps certificate rotation possible while
// refusing an unverified transport for pairing secrets and signed events.
constexpr char kDigiCertGlobalRootG2[] = R"PEM(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----)PEM";

bool copyBounded(char* target, const size_t capacity, const char* value) {
  if (!target || capacity == 0 || !value) return false;
  const size_t length = strlen(value);
  if (length == 0 || length >= capacity) return false;
  memcpy(target, value, length + 1);
  return true;
}

void hexDigest(const uint8_t digest[32], char output[65]) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < 32; ++i) {
    output[i * 2] = kHex[digest[i] >> 4u];
    output[i * 2 + 1] = kHex[digest[i] & 0x0fu];
  }
  output[64] = '\0';
}

uint64_t epochMillis() {
  const time_t now = time(nullptr);
  if (now <= 0) return 0;
  return static_cast<uint64_t>(now) * 1000ULL;
}

bool validIdentifier(const char* value, const size_t minimum, const size_t maximum) {
  if (!value) return false;
  const size_t length = strlen(value);
  if (length < minimum || length > maximum) return false;
  return std::all_of(value, value + length, [](const char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}

}  // namespace

bool AirPageIGrowthBridge::begin(const char* deviceId, const igrowth::ServiceEnvironment environment,
                                 const std::string& developerOrigin) {
  igrowth::ServiceEndpoint endpoint;
  if (!igrowth::resolveServiceEndpoint(environment, developerOrigin, endpoint)) {
    LOG_ERR("AIRIG", "Invalid iGrowth service endpoint");
    return false;
  }
  endpoint_ = std::move(endpoint);
  clearManifest();
  pairingId_[0] = '\0';
  claimToken_[0] = '\0';
  displayCode_[0] = '\0';
  pairingExpiresAtMs_ = 0;
  if (!copyBounded(deviceId_, sizeof(deviceId_), deviceId)) {
    LOG_ERR("AIRIG", "Invalid AirPage device id");
    deviceId_[0] = '\0';
    return false;
  }
  loadCredential();
  sequence_ = loadSequence();
  return true;
}

bool AirPageIGrowthBridge::loadCredential() {
  bindingRevision_[0] = '\0';
  secret_[0] = '\0';
  char state[kBindingCapacity + 96]{};
  const std::string credentialPath = igrowth::statePath(endpoint_, "credential");
  const size_t read = Storage.readFileToBuffer(credentialPath.c_str(), state, sizeof(state));
  if (read == 0) return false;
  char* separator = strchr(state, '\n');
  if (!separator) return false;
  *separator = '\0';
  bool decoded = false;
  const std::string secret = obfuscation::deobfuscateFromBase64(separator + 1, kSecretCapacity - 1, &decoded, nullptr);
  if (!decoded || !validIdentifier(state, 9, kBindingCapacity - 1) ||
      !validIdentifier(secret.c_str(), 43, kSecretCapacity - 1) ||
      !copyBounded(bindingRevision_, sizeof(bindingRevision_), state) ||
      !copyBounded(secret_, sizeof(secret_), secret.c_str())) {
    bindingRevision_[0] = '\0';
    secret_[0] = '\0';
    return false;
  }
  return true;
}

bool AirPageIGrowthBridge::saveCredential() const {
  if (!paired() || !Storage.ensureDirectoryExists(endpoint_.stateDirectory.c_str())) return false;
  const String obfuscated = obfuscation::obfuscateToBase64(secret_);
  if (obfuscated.isEmpty()) return false;
  char state[kBindingCapacity + 96];
  const int written = snprintf(state, sizeof(state), "%s\n%s", bindingRevision_, obfuscated.c_str());
  const std::string credentialPath = igrowth::statePath(endpoint_, "credential");
  return written > 0 && static_cast<size_t>(written) < sizeof(state) &&
         Storage.writeFile(credentialPath.c_str(), String(state));
}

uint64_t AirPageIGrowthBridge::loadSequence() const {
  char value[24]{};
  const std::string sequencePath = igrowth::statePath(endpoint_, "sequence");
  if (Storage.readFileToBuffer(sequencePath.c_str(), value, sizeof(value)) == 0) return 0;
  char* end = nullptr;
  const unsigned long long sequence = strtoull(value, &end, 10);
  return end && *end == '\0' ? static_cast<uint64_t>(sequence) : 0;
}

bool AirPageIGrowthBridge::saveSequence(const uint64_t sequence) const {
  if (!Storage.ensureDirectoryExists(endpoint_.stateDirectory.c_str())) return false;
  char value[24];
  const int written = snprintf(value, sizeof(value), "%llu", static_cast<unsigned long long>(sequence));
  const std::string sequencePath = igrowth::statePath(endpoint_, "sequence");
  return written > 0 && static_cast<size_t>(written) < sizeof(value) &&
         Storage.writeFile(sequencePath.c_str(), String(value));
}

AirPageIGrowthBridge::PairingResult AirPageIGrowthBridge::startPairing() {
  if (deviceId_[0] == '\0') return PairingResult::Invalid;
  std::string body;
  body.reserve(48);
  body = "{\"device_id\":\"";
  body += deviceId_;
  body += "\"}";
  int status = 0;
  std::string response;
  if (!postJson(kPairingPath, body, false, status, response)) return PairingResult::NetworkFailed;
  if (status != 200) return PairingResult::Invalid;
  JsonDocument doc;
  if (deserializeJson(doc, response)) {
    pairingId_[0] = '\0';
    claimToken_[0] = '\0';
    displayCode_[0] = '\0';
    pairingExpiresAtMs_ = 0;
    return PairingResult::Invalid;
  }
  const uint64_t expiresAtMs = doc["expires_at_ms"].as<uint64_t>();
  if (strcmp(doc["status"] | "", "pending") != 0 ||
      !copyBounded(pairingId_, sizeof(pairingId_), doc["pairing_id"] | "") ||
      !copyBounded(claimToken_, sizeof(claimToken_), doc["claim_token"] | "") ||
      !copyBounded(displayCode_, sizeof(displayCode_), doc["display_code"] | "") ||
      expiresAtMs < kMinimumValidEpochMs) {
    pairingId_[0] = '\0';
    claimToken_[0] = '\0';
    displayCode_[0] = '\0';
    pairingExpiresAtMs_ = 0;
    return PairingResult::Invalid;
  }
  pairingExpiresAtMs_ = expiresAtMs;
  return PairingResult::Started;
}

AirPageIGrowthBridge::PairingResult AirPageIGrowthBridge::claimPairing() {
  if (!pairingPending()) return PairingResult::Invalid;
  const uint64_t nowMs = epochMillis();
  if (pairingExpiresAtMs_ != 0 && nowMs >= pairingExpiresAtMs_) {
    pairingId_[0] = '\0';
    claimToken_[0] = '\0';
    displayCode_[0] = '\0';
    pairingExpiresAtMs_ = 0;
    return PairingResult::Invalid;
  }
  std::string body;
  body.reserve(224);
  body = "{\"pairing_id\":\"";
  body += pairingId_;
  body += "\",\"claim_token\":\"";
  body += claimToken_;
  body += "\"}";
  int status = 0;
  std::string response;
  if (!postJson(kClaimPath, body, false, status, response)) return PairingResult::NetworkFailed;
  if (status == 404) return PairingResult::Pending;
  if (status != 200) return PairingResult::Invalid;
  JsonDocument doc;
  if (deserializeJson(doc, response) || strcmp(doc["status"] | "", "paired") != 0 ||
      !copyBounded(bindingRevision_, sizeof(bindingRevision_), doc["binding_revision"] | "") ||
      !copyBounded(secret_, sizeof(secret_), doc["device_secret"] | "") || !saveCredential()) {
    bindingRevision_[0] = '\0';
    secret_[0] = '\0';
    return PairingResult::Invalid;
  }
  pairingId_[0] = '\0';
  claimToken_[0] = '\0';
  displayCode_[0] = '\0';
  pairingExpiresAtMs_ = 0;
  return PairingResult::Paired;
}

bool AirPageIGrowthBridge::inspectIGrowthImage(const char* imagePath, char* imageSha256, const size_t imageSha256Size,
                                               uint32_t& trailerPage) const {
  trailerPage = 0;
  if (!imagePath || !imageSha256 || imageSha256Size < kShaCapacity) return false;
  HalFile file;
  if (!Storage.openFileForRead("AIRIG", imagePath, file)) return false;
  const size_t fileSize = file.fileSize();
  if (fileSize < igrowth::kDeliveryTrailerSize || fileSize > kMaxIGrowthBmpBytes ||
      !file.seek(fileSize - igrowth::kDeliveryTrailerSize)) {
    return false;
  }
  uint8_t trailer[igrowth::kDeliveryTrailerSize];
  if (file.read(trailer, sizeof(trailer)) != static_cast<int>(sizeof(trailer)) ||
      !igrowth::parseDeliveryTrailer(trailer, sizeof(trailer), trailerPage) || !file.seek(0)) {
    return false;
  }

  auto buffer = makeUniqueNoThrow<uint8_t[]>(512);
  if (!buffer) {
    LOG_ERR("AIRIG", "OOM: 512 byte image hash buffer");
    return false;
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  size_t consumed = 0;
  while (ok && consumed < fileSize) {
    const size_t wanted = std::min<size_t>(512, fileSize - consumed);
    const int read = file.read(buffer.get(), wanted);
    if (read <= 0 || static_cast<size_t>(read) > wanted) {
      ok = false;
      break;
    }
    ok = mbedtls_sha256_update(&context, buffer.get(), static_cast<size_t>(read)) == 0;
    consumed += static_cast<size_t>(read);
  }
  uint8_t digest[32];
  ok = ok && consumed == fileSize && mbedtls_sha256_finish(&context, digest) == 0;
  mbedtls_sha256_free(&context);
  if (!ok) return false;
  hexDigest(digest, imageSha256);
  return true;
}

AirPageIGrowthBridge::ManifestResult AirPageIGrowthBridge::loadManifest(const char* imagePath,
                                                                        const bool networkAvailable) {
  clearManifest();
  if (!paired()) return ManifestResult::NotPaired;
  uint32_t trailerPage = 0;
  char imageSha[kShaCapacity];
  if (!inspectIGrowthImage(imagePath, imageSha, sizeof(imageSha), trailerPage)) return ManifestResult::NotIGrowth;
  if (!networkAvailable) {
    return loadCachedManifest(imageSha, trailerPage) ? ManifestResult::Ready : ManifestResult::Unavailable;
  }
  std::string body;
  body.reserve(96);
  body = "{\"bmp_sha256\":\"";
  body += imageSha;
  body += "\"}";
  int status = 0;
  std::string response;
  if (!postJson(kManifestPath, body, true, status, response)) {
    return status < 0 && loadCachedManifest(imageSha, trailerPage) ? ManifestResult::Ready
                                                                   : ManifestResult::Unavailable;
  }
  if (status != 200) return ManifestResult::Unavailable;
  JsonDocument doc;
  if (deserializeJson(doc, response)) return ManifestResult::Unavailable;
  const JsonArrayConst actions = doc["actions"].as<JsonArrayConst>();
  if (static_cast<int>(doc["version"] | 0) != 1 || static_cast<uint32_t>(doc["page_number"] | 0u) != trailerPage ||
      strcmp(doc["image_sha256"] | "", imageSha) != 0 || !doc["actions"].is<JsonArrayConst>() || actions.size() != 4 ||
      !copyBounded(deliveryId_, sizeof(deliveryId_), doc["delivery_id"] | "") ||
      !copyBounded(imageSha256_, sizeof(imageSha256_), imageSha)) {
    clearManifest();
    return ManifestResult::Unavailable;
  }
  for (uint8_t index = 0; index < 4; ++index) {
    const auto& expected = igrowth::actionContract(static_cast<igrowth::Button>(index));
    const JsonObjectConst action = actions[index].as<JsonObjectConst>();
    if (strcmp(action["button"] | "", expected.button) != 0 ||
        strcmp(action["action_id"] | "", expected.actionId) != 0) {
      clearManifest();
      return ManifestResult::Unavailable;
    }
  }
  pageNumber_ = trailerPage;
  manifestReady_ = true;
  if (!saveCachedManifest()) LOG_ERR("AIRIG", "Could not cache action manifest");
  return ManifestResult::Ready;
}

bool AirPageIGrowthBridge::loadCachedManifest(const char* imageSha256, const uint32_t pageNumber) {
  char state[kBindingCapacity + kDeliveryIdCapacity + kShaCapacity + 28]{};
  const std::string manifestPath = igrowth::statePath(endpoint_, "manifest");
  if (Storage.readFileToBuffer(manifestPath.c_str(), state, sizeof(state)) == 0) return false;
  char* binding = state;
  char* delivery = strchr(binding, '\n');
  if (!delivery) return false;
  *delivery++ = '\0';
  char* sha = strchr(delivery, '\n');
  if (!sha) return false;
  *sha++ = '\0';
  char* page = strchr(sha, '\n');
  if (!page) return false;
  *page++ = '\0';
  char* end = nullptr;
  const unsigned long cachedPage = strtoul(page, &end, 10);
  if (!end || *end != '\0' || cachedPage != pageNumber || strcmp(binding, bindingRevision_) != 0 ||
      strcmp(sha, imageSha256) != 0 || !validIdentifier(delivery, 9, kDeliveryIdCapacity - 1) ||
      !copyBounded(deliveryId_, sizeof(deliveryId_), delivery) ||
      !copyBounded(imageSha256_, sizeof(imageSha256_), imageSha256)) {
    clearManifest();
    return false;
  }
  pageNumber_ = pageNumber;
  manifestReady_ = true;
  return true;
}

bool AirPageIGrowthBridge::saveCachedManifest() const {
  if (!manifestReady_ || !Storage.ensureDirectoryExists(endpoint_.stateDirectory.c_str())) return false;
  char state[kBindingCapacity + kDeliveryIdCapacity + kShaCapacity + 28];
  const int written = snprintf(state, sizeof(state), "%s\n%s\n%s\n%lu", bindingRevision_, deliveryId_, imageSha256_,
                               static_cast<unsigned long>(pageNumber_));
  const std::string manifestPath = igrowth::statePath(endpoint_, "manifest");
  return written > 0 && static_cast<size_t>(written) < sizeof(state) &&
         Storage.writeFile(manifestPath.c_str(), String(state));
}

void AirPageIGrowthBridge::clearManifest() {
  manifestReady_ = false;
  deliveryId_[0] = '\0';
  imageSha256_[0] = '\0';
  pageNumber_ = 0;
}

int AirPageIGrowthBridge::postEvent(const std::string& body) const {
  int status = 0;
  std::string response;
  if (!postJson(kEventPath, body, true, status, response)) return -1;
  return status;
}

AirPageIGrowthBridge::ActionResult AirPageIGrowthBridge::sendAction(const igrowth::Button button,
                                                                    const bool networkAvailable) {
  if (!paired() || !manifestReady_) return ActionResult::Unavailable;
  const uint64_t nowMs = epochMillis();
  if (nowMs < kMinimumValidEpochMs) return ActionResult::Unavailable;
  const uint64_t sequence = sequence_ + 1;
  if (!saveSequence(sequence)) return ActionResult::Unavailable;
  sequence_ = sequence;
  const auto& action = igrowth::actionContract(button);
  char eventId[39];
  snprintf(eventId, sizeof(eventId), "event-%08lx%08lx%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()), static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));

  std::string body;
  body.reserve(448);
  JsonDocument doc;
  doc["event_id"] = eventId;
  doc["delivery_id"] = deliveryId_;
  doc["image_sha256"] = imageSha256_;
  doc["action_id"] = action.actionId;
  doc["button"] = action.button;
  doc["page_number"] = pageNumber_;
  doc["sequence"] = sequence;
  doc["created_at_ms"] = nowMs;
  serializeJson(doc, body);
  if (body.empty() || body.size() > kMaxEventBytes) return ActionResult::Unavailable;

  const int status = networkAvailable ? postEvent(body) : -1;
  if (status >= 200 && status < 300) return ActionResult::Accepted;
  if (status < 0 || status >= 500) {
    return saveQueuedEvent(sequence, body) ? ActionResult::QueuedOffline : ActionResult::Unavailable;
  }
  LOG_ERR("AIRIG", "Event rejected permanently: %d", status);
  return ActionResult::Unavailable;
}

bool AirPageIGrowthBridge::findOldestQueuedEvent(char* path, const size_t pathSize, size_t* count) const {
  if (!path || pathSize == 0) return false;
  path[0] = '\0';
  size_t found = 0;
  const std::string outboxDirectory = igrowth::statePath(endpoint_, "outbox");
  auto directory = Storage.open(outboxDirectory.c_str());
  if (!directory || !directory.isDirectory()) {
    if (count) *count = 0;
    return false;
  }
  directory.rewindDirectory();
  for (auto file = directory.openNextFile(); file; file = directory.openNextFile()) {
    if (file.isDirectory()) continue;
    char name[96];
    if (!file.getName(name, sizeof(name))) continue;
    const char* base = strrchr(name, '/');
    base = base ? base + 1 : name;
    const size_t length = strlen(base);
    if (length != 25 || strcmp(base + 20, ".json") != 0) continue;
    ++found;
    char candidate[96];
    const int written = snprintf(candidate, sizeof(candidate), "%s/%s", outboxDirectory.c_str(), base);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(candidate)) continue;
    if (path[0] == '\0' || strcmp(candidate, path) < 0) snprintf(path, pathSize, "%s", candidate);
  }
  if (count) *count = found;
  return path[0] != '\0';
}

bool AirPageIGrowthBridge::saveQueuedEvent(const uint64_t sequence, const std::string& body) const {
  const std::string outboxDirectory = igrowth::statePath(endpoint_, "outbox");
  if (!Storage.ensureDirectoryExists(outboxDirectory.c_str())) return false;
  char oldest[96];
  size_t count = 0;
  findOldestQueuedEvent(oldest, sizeof(oldest), &count);
  if (count >= kMaxOutboxEntries) return false;
  char path[96];
  const int written = snprintf(path, sizeof(path), "%s/%020llu.json", outboxDirectory.c_str(),
                               static_cast<unsigned long long>(sequence));
  return written > 0 && static_cast<size_t>(written) < sizeof(path) && Storage.writeFile(path, String(body.c_str()));
}

bool AirPageIGrowthBridge::drainOneQueuedEvent() {
  if (!paired()) return false;
  char path[96];
  if (!findOldestQueuedEvent(path, sizeof(path))) return false;
  auto body = makeUniqueNoThrow<char[]>(kMaxEventBytes + 1);
  if (!body) {
    LOG_ERR("AIRIG", "OOM: %u byte outbox buffer", static_cast<unsigned>(kMaxEventBytes + 1));
    return false;
  }
  const size_t read = Storage.readFileToBuffer(path, body.get(), kMaxEventBytes + 1, kMaxEventBytes);
  if (read == 0 || read > kMaxEventBytes) {
    Storage.remove(path);
    return false;
  }
  const int status = postEvent(std::string(body.get(), read));
  if ((status >= 200 && status < 300) || (status >= 400 && status < 500)) {
    if (!Storage.remove(path)) LOG_ERR("AIRIG", "Could not remove settled outbox event");
    return true;
  }
  return false;
}

bool AirPageIGrowthBridge::postJson(const char* path, const std::string& body, const bool signedRequest, int& status,
                                    std::string& response) const {
  status = -1;
  response.clear();
  if (!path || body.size() > kMaxEventBytes) return false;
  std::string url;
  url.reserve(endpoint_.origin.size() + strlen(path));
  url = endpoint_.origin;
  url += path;
  freeink::SecureHttpClient http;
  http.setTimeout(20000);
  if (endpoint_.tls) http.setCACert(kDigiCertGlobalRootG2);
  http.setReuse(false);
#ifndef SIMULATOR
  http.setUserAgent("CrossMux-AirPage-iGrowth/1");
#endif
  if (!http.begin(url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  if (signedRequest) {
    const uint64_t timestampMs = epochMillis();
    if (!paired() || timestampMs < kMinimumValidEpochMs) return false;
    uint8_t bodyDigest[32];
    char bodySha[65];
    if (!crypto::sha256(reinterpret_cast<const uint8_t*>(body.data()), body.size(), bodyDigest)) return false;
    hexDigest(bodyDigest, bodySha);
    char canonical[224];
    if (!igrowth::buildCanonicalRequest("POST", path, timestampMs, bodySha, canonical, sizeof(canonical))) return false;
    uint8_t signatureDigest[32];
    if (!crypto::hmacSha256(reinterpret_cast<const uint8_t*>(secret_), strlen(secret_),
                            reinterpret_cast<const uint8_t*>(canonical), strlen(canonical), signatureDigest)) {
      return false;
    }
    char signature[65];
    char timestamp[24];
    hexDigest(signatureDigest, signature);
    snprintf(timestamp, sizeof(timestamp), "%llu", static_cast<unsigned long long>(timestampMs));
    http.addHeader("X-AirPage-Device-ID", deviceId_);
    http.addHeader("X-AirPage-Timestamp", timestamp);
    http.addHeader("X-AirPage-Signature", signature);
  }

  bool responseTooLarge = false;
#ifdef SIMULATOR
  status = http.sendRequest("POST", body);
  const String bufferedResponse = http.getString();
  responseTooLarge = bufferedResponse.length() > kMaxResponseBytes;
  if (!responseTooLarge) response.assign(bufferedResponse.c_str(), bufferedResponse.length());
  if (status < 0 || responseTooLarge) return false;
#else
  status = http.sendRequest("POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
                            [&response, &responseTooLarge](const uint8_t* bytes, const size_t size) {
                              if (response.size() + size > kMaxResponseBytes) {
                                responseTooLarge = true;
                                return false;
                              }
                              response.append(reinterpret_cast<const char*>(bytes), size);
                              return true;
                            });
  if (status < 0 || responseTooLarge || http.callbackAborted() || !http.responseComplete()) return false;
#endif
  return true;
}

}  // namespace airpage
