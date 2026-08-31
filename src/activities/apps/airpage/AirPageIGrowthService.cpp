#include "AirPageIGrowthService.h"

#include <array>
#include <cctype>

namespace airpage::igrowth {

namespace {

constexpr char kProductionOrigin[] = "https://igrowth.cc";
// Keep the existing production directory so upgrades preserve pairings and
// queued actions. Development is a sibling tree and can never reuse them.
constexpr char kProductionStateDirectory[] = "/.crosspoint/airpage/igrowth";
constexpr char kDevelopmentStateDirectory[] = "/.crosspoint/airpage/igrowth-development";
constexpr char kHttpPrefix[] = "http://";

std::string trimAsciiWhitespace(const std::string& value) {
  size_t first = 0;
  while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
  size_t last = value.size();
  while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
  return value.substr(first, last - first);
}

bool parseIpv4(const std::string& host, std::array<uint8_t, 4>& octets) {
  size_t start = 0;
  for (size_t index = 0; index < octets.size(); ++index) {
    const size_t end = host.find('.', start);
    if ((index < octets.size() - 1 && end == std::string::npos) ||
        (index == octets.size() - 1 && end != std::string::npos)) {
      return false;
    }
    const size_t partEnd = end == std::string::npos ? host.size() : end;
    if (partEnd == start || partEnd - start > 3) return false;
    unsigned value = 0;
    for (size_t position = start; position < partEnd; ++position) {
      const char c = host[position];
      if (c < '0' || c > '9') return false;
      value = value * 10u + static_cast<unsigned>(c - '0');
    }
    if (value > 255u) return false;
    octets[index] = static_cast<uint8_t>(value);
    start = partEnd + 1;
  }
  return start == host.size() + 1;
}

bool isPrivateIpv4(const std::string& host) {
  std::array<uint8_t, 4> octets{};
  if (!parseIpv4(host, octets)) return false;
  if (octets[0] == 10) return true;
  if (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31) return true;
  return octets[0] == 192 && octets[1] == 168;
}

bool isLocalMdnsName(const std::string& host) {
  constexpr char suffix[] = ".local";
  if (host.size() <= sizeof(suffix) - 1 || host.compare(host.size() - (sizeof(suffix) - 1), sizeof(suffix) - 1,
                                                        suffix) != 0) {
    return false;
  }
  bool labelHasCharacter = false;
  for (const char c : host) {
    if (c == '.') {
      if (!labelHasCharacter) return false;
      labelHasCharacter = false;
      continue;
    }
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-') return false;
    labelHasCharacter = true;
  }
  return labelHasCharacter;
}

bool validDevelopmentOrigin(const std::string& origin) {
  if (origin.rfind(kHttpPrefix, 0) != 0) return false;
  const std::string authority = origin.substr(sizeof(kHttpPrefix) - 1);
  if (authority.empty() || authority.find_first_of("/?#@") != std::string::npos) return false;
  const size_t colon = authority.rfind(':');
  if (colon == std::string::npos) return false;
  const std::string portText = authority.substr(colon + 1);
  if (portText.empty() || portText.size() > 5) return false;
  unsigned port = 0;
  for (const char c : portText) {
    if (c < '0' || c > '9') return false;
    port = port * 10u + static_cast<unsigned>(c - '0');
  }
  if (port < 1024u || port > 65535u) return false;
  const std::string host = authority.substr(0, colon);
  return isPrivateIpv4(host) || isLocalMdnsName(host);
}

}  // namespace

bool resolveServiceEndpoint(const ServiceEnvironment environment, const std::string& developerOrigin,
                            ServiceEndpoint& endpoint) {
  if (environment == ServiceEnvironment::Production) {
    endpoint.origin = kProductionOrigin;
    endpoint.stateDirectory = kProductionStateDirectory;
    endpoint.tls = true;
    return true;
  }
  if (environment != ServiceEnvironment::Development) return false;

  const std::string origin = trimAsciiWhitespace(developerOrigin);
  if (!validDevelopmentOrigin(origin)) return false;
  endpoint.origin = origin;
  endpoint.stateDirectory = kDevelopmentStateDirectory;
  endpoint.tls = false;
  return true;
}

ServiceEnvironment parseServiceEnvironment(const std::string& value) {
  return value == "development" ? ServiceEnvironment::Development : ServiceEnvironment::Production;
}

const char* serviceEnvironmentValue(const ServiceEnvironment environment) {
  return environment == ServiceEnvironment::Development ? "development" : "production";
}

std::string statePath(const ServiceEndpoint& endpoint, const char* leaf) {
  if (!leaf || leaf[0] == '\0') return endpoint.stateDirectory;
  return endpoint.stateDirectory + "/" + leaf;
}

}  // namespace airpage::igrowth
