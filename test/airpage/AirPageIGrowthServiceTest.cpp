#include <gtest/gtest.h>

#include "AirPageIGrowthService.h"

namespace {

using airpage::igrowth::ServiceEndpoint;
using airpage::igrowth::ServiceEnvironment;

TEST(AirPageIGrowthServiceTest, ProductionAlwaysUsesPinnedHttpsOriginAndProductionState) {
  ServiceEndpoint endpoint;

  ASSERT_TRUE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Production, "", endpoint));
  EXPECT_EQ(endpoint.origin, "https://igrowth.cc");
  EXPECT_TRUE(endpoint.tls);
  EXPECT_EQ(endpoint.stateDirectory, "/.crosspoint/airpage/igrowth");
}

TEST(AirPageIGrowthServiceTest, DevelopmentAcceptsPrivateLanIpv4Origins) {
  const char* origins[] = {
      "http://192.168.31.8:2048",
      "http://192.168.31.8:2148",
      "http://10.0.0.7:2048",
      "http://172.16.0.2:2048",
      "http://172.31.255.254:2048",
  };

  for (const char* origin : origins) {
    ServiceEndpoint endpoint;
    ASSERT_TRUE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Development, origin, endpoint))
        << origin;
    EXPECT_EQ(endpoint.origin, origin);
    EXPECT_FALSE(endpoint.tls);
    EXPECT_EQ(endpoint.stateDirectory, "/.crosspoint/airpage/igrowth-development");
  }
}

TEST(AirPageIGrowthServiceTest, DevelopmentAcceptsLocalMdnsAndTrimsSdCardNewline) {
  ServiceEndpoint endpoint;

  ASSERT_TRUE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Development,
                                                       "  http://igrowth.local:2048\r\n", endpoint));
  EXPECT_EQ(endpoint.origin, "http://igrowth.local:2048");
  EXPECT_FALSE(endpoint.tls);
}

TEST(AirPageIGrowthServiceTest, DevelopmentRejectsPublicOrAmbiguousPlaintextOrigins) {
  const char* origins[] = {
      "http://igrowth.cc:2048",
      "http://8.8.8.8:2048",
      "http://172.32.0.1:2048",
      "http://127.0.0.1:2048",
      "http://192.168.1.2",
      "http://192.168.1.2:80",
      "http://192.168.1.2:70000",
      "http://192.168.1.2:not-a-port",
      "http://192.168.1.2:2048/path",
      "http://user:pass@192.168.1.2:2048",
      "https://192.168.1.2:2048",
      "192.168.1.2:2048",
      "",
  };

  for (const char* origin : origins) {
    ServiceEndpoint endpoint;
    EXPECT_FALSE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Development, origin, endpoint))
        << origin;
  }
}

TEST(AirPageIGrowthServiceTest, EnvironmentsUseSeparateCredentialAndOutboxTrees) {
  ServiceEndpoint production;
  ServiceEndpoint development;
  ASSERT_TRUE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Production, "", production));
  ASSERT_TRUE(airpage::igrowth::resolveServiceEndpoint(ServiceEnvironment::Development,
                                                       "http://192.168.1.2:2148", development));

  EXPECT_NE(production.stateDirectory, development.stateDirectory);
  EXPECT_EQ(airpage::igrowth::statePath(production, "credential"),
            "/.crosspoint/airpage/igrowth/credential");
  EXPECT_EQ(airpage::igrowth::statePath(development, "outbox"),
            "/.crosspoint/airpage/igrowth-development/outbox");
}

TEST(AirPageIGrowthServiceTest, PersistedEnvironmentDefaultsSafelyToProduction) {
  EXPECT_EQ(airpage::igrowth::parseServiceEnvironment("development"), ServiceEnvironment::Development);
  EXPECT_EQ(airpage::igrowth::parseServiceEnvironment("production"), ServiceEnvironment::Production);
  EXPECT_EQ(airpage::igrowth::parseServiceEnvironment("garbage"), ServiceEnvironment::Production);
  EXPECT_EQ(airpage::igrowth::parseServiceEnvironment(""), ServiceEnvironment::Production);
  EXPECT_STREQ(airpage::igrowth::serviceEnvironmentValue(ServiceEnvironment::Production), "production");
  EXPECT_STREQ(airpage::igrowth::serviceEnvironmentValue(ServiceEnvironment::Development), "development");
}

}  // namespace
