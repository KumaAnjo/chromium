// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/command_line.h"
#include "chrome/browser/policy/mock_configuration_policy_provider.h"
#include "chrome/browser/policy/policy_service_impl.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/prefs/pref_service_mock_builder.h"
#include "chrome/browser/prefs/proxy_config_dictionary.h"
#include "chrome/browser/prefs/proxy_prefs.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "policy/policy_constants.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

void assertProxyMode(const ProxyConfigDictionary& dict,
                     ProxyPrefs::ProxyMode expected_mode) {
  ProxyPrefs::ProxyMode actual_mode;
  ASSERT_TRUE(dict.GetMode(&actual_mode));
  EXPECT_EQ(expected_mode, actual_mode);
}

void assertProxyServer(const ProxyConfigDictionary& dict,
                       const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetProxyServer(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetProxyServer(&actual));
  }
}

void assertPacUrl(const ProxyConfigDictionary& dict,
                  const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetPacUrl(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetPacUrl(&actual));
  }
}

void assertBypassList(const ProxyConfigDictionary& dict,
                      const std::string& expected) {
  std::string actual;
  if (!expected.empty()) {
    ASSERT_TRUE(dict.GetBypassList(&actual));
    EXPECT_EQ(expected, actual);
  } else {
    EXPECT_FALSE(dict.GetBypassList(&actual));
  }
}

void assertProxyModeWithoutParams(const ProxyConfigDictionary& dict,
                                  ProxyPrefs::ProxyMode proxy_mode) {
  assertProxyMode(dict, proxy_mode);
  assertProxyServer(dict, "");
  assertPacUrl(dict, "");
  assertBypassList(dict, "");
}

class ProxyPolicyTest : public testing::Test {
 protected:
  ProxyPolicyTest()
      : command_line_(CommandLine::NO_PROGRAM) {}

  virtual void SetUp() OVERRIDE {
   policy::PolicyServiceImpl::Providers providers;
   providers.push_back(&provider_);
   policy_service_.reset(new policy::PolicyServiceImpl(providers));
  }

  PrefService* CreatePrefService(bool with_managed_policies) {
    PrefServiceMockBuilder builder;
    builder.WithCommandLine(&command_line_);
    if (with_managed_policies)
      builder.WithManagedPolicies(policy_service_.get());
    PrefService* prefs = builder.Create();
    browser::RegisterUserPrefs(prefs);
    return prefs;
  }

  CommandLine command_line_;
  policy::MockConfigurationPolicyProvider provider_;
  scoped_ptr<policy::PolicyServiceImpl> policy_service_;
};

TEST_F(ProxyPolicyTest, OverridesCommandLineOptions) {
  command_line_.AppendSwitchASCII(switches::kProxyBypassList, "123");
  command_line_.AppendSwitchASCII(switches::kProxyServer, "789");
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kFixedServersProxyModeName);
  provider_.AddMandatoryPolicy(policy::key::kProxyMode, mode_name);
  provider_.AddMandatoryPolicy(policy::key::kProxyBypassList,
                               Value::CreateStringValue("abc"));
  provider_.AddMandatoryPolicy(policy::key::kProxyServer,
                               Value::CreateStringValue("ghi"));
  provider_.RefreshPolicies();

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  scoped_ptr<PrefService> prefs(CreatePrefService(false));
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyMode(dict, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict, "789");
  assertPacUrl(dict, "");
  assertBypassList(dict, "123");

  // Try a second time time with the managed PrefStore in place, the
  // manual proxy policy should have removed all traces of the command
  // line and replaced them with the policy versions.
  prefs.reset(CreatePrefService(true));
  ProxyConfigDictionary dict2(prefs->GetDictionary(prefs::kProxy));
  assertProxyMode(dict2, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict2, "ghi");
  assertPacUrl(dict2, "");
  assertBypassList(dict2, "abc");
}

TEST_F(ProxyPolicyTest, OverridesUnrelatedCommandLineOptions) {
  command_line_.AppendSwitchASCII(switches::kProxyBypassList, "123");
  command_line_.AppendSwitchASCII(switches::kProxyServer, "789");
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kAutoDetectProxyModeName);
  provider_.AddMandatoryPolicy(policy::key::kProxyMode, mode_name);
  provider_.RefreshPolicies();

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  scoped_ptr<PrefService> prefs(CreatePrefService(false));
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyMode(dict, ProxyPrefs::MODE_FIXED_SERVERS);
  assertProxyServer(dict, "789");
  assertPacUrl(dict, "");
  assertBypassList(dict, "123");

  // Try a second time time with the managed PrefStore in place, the
  // no proxy policy should have removed all traces of the command
  // line proxy settings, even though they were not the specific one
  // set in policy.
  prefs.reset(CreatePrefService(true));
  ProxyConfigDictionary dict2(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_AUTO_DETECT);
}

TEST_F(ProxyPolicyTest, OverridesCommandLineNoProxy) {
  command_line_.AppendSwitch(switches::kNoProxyServer);
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kAutoDetectProxyModeName);
  provider_.AddMandatoryPolicy(policy::key::kProxyMode, mode_name);
  provider_.RefreshPolicies();

  // First verify that command-line options are set correctly when
  // there is no policy in effect.
  scoped_ptr<PrefService> prefs(CreatePrefService(false));
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict, ProxyPrefs::MODE_DIRECT);

  // Try a second time time with the managed PrefStore in place, the
  // auto-detect should be overridden. The default pref store must be
  // in place with the appropriate default value for this to work.
  prefs.reset(CreatePrefService(true));
  ProxyConfigDictionary dict2(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_AUTO_DETECT);
}

TEST_F(ProxyPolicyTest, OverridesCommandLineAutoDetect) {
  command_line_.AppendSwitch(switches::kProxyAutoDetect);
  Value* mode_name = Value::CreateStringValue(
      ProxyPrefs::kDirectProxyModeName);
  provider_.AddMandatoryPolicy(policy::key::kProxyMode, mode_name);
  provider_.RefreshPolicies();

  // First verify that the auto-detect is set if there is no managed
  // PrefStore.
  scoped_ptr<PrefService> prefs(CreatePrefService(false));
  ProxyConfigDictionary dict(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict, ProxyPrefs::MODE_AUTO_DETECT);

  // Try a second time time with the managed PrefStore in place, the
  // auto-detect should be overridden. The default pref store must be
  // in place with the appropriate default value for this to work.
  prefs.reset(CreatePrefService(true));
  ProxyConfigDictionary dict2(prefs->GetDictionary(prefs::kProxy));
  assertProxyModeWithoutParams(dict2, ProxyPrefs::MODE_DIRECT);
}

}  // namespace
