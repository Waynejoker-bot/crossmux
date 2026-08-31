#pragma once

#include <cstdint>
#include <string>

namespace airpage::igrowth {

enum class ServiceEnvironment : uint8_t {
  Production = 0,
  Development = 1,
};

struct ServiceEndpoint {
  std::string origin;
  std::string stateDirectory;
  bool tls = true;
};

// Resolves the selected iGrowth environment without ever permitting cleartext
// traffic beyond the local network. Production ignores developerOrigin and is
// always the pinned public HTTPS service. Development requires an explicit
// http:// origin on a non-privileged port whose host is private IPv4 or an
// mDNS .local name.
bool resolveServiceEndpoint(ServiceEnvironment environment, const std::string& developerOrigin,
                            ServiceEndpoint& endpoint);
ServiceEnvironment parseServiceEnvironment(const std::string& value);
const char* serviceEnvironmentValue(ServiceEnvironment environment);
std::string statePath(const ServiceEndpoint& endpoint, const char* leaf);

}  // namespace airpage::igrowth
