/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
 * @file testThinClientSecurityPostAuthorization.cpp
 *
 * @brief Currently this tests for getAll() with only some keys allowed
 *        so expecting some authorization failure exceptions in the result
 *
 *
 */

#include "fw_dunit.hpp"
#include <geode/GeodeCppCache.hpp>
#include "CacheHelper.hpp"
#include "ThinClientHelper.hpp"
#include <ace/Process.h>
#include <string>

#include "ThinClientSecurity.hpp"

using namespace apache::geode::client;

const char* locHostPort =
    CacheHelper::getLocatorHostPort(isLocator, isLocalServer, 1);

#define HANDLE_NO_NOT_AUTHORIZED_EXCEPTION                       \
  catch (const apache::geode::client::NotAuthorizedException&) { \
    LOG("NotAuthorizedException Caught");                        \
    FAIL("should not have caught NotAuthorizedException");       \
  }                                                              \
  catch (const apache::geode::client::Exception& other) {        \
    LOG("Got apache::geode::client::Exception& other ");         \
    other.printStackTrace();                                     \
    FAIL(other.getMessage());                                    \
  }

#define HANDLE_NOT_AUTHORIZED_EXCEPTION                          \
  catch (const apache::geode::client::NotAuthorizedException&) { \
    LOG("NotAuthorizedException Caught");                        \
    LOG("Success");                                              \
  }                                                              \
  catch (const apache::geode::client::Exception& other) {        \
    other.printStackTrace();                                     \
    FAIL(other.getMessage());                                    \
  }

#define ADMIN_CLIENT s1p1
#define WRITER_CLIENT s1p2
#define READER_CLIENT s2p1
#define READER2_CLIENT s2p2

const char* regionNamesAuth[] = {"DistRegionAck"};

void initClientAuth(char userType, int clientNum = 1) {
  PropertiesPtr config = Properties::create();
  config->insert("security-client-auth-library", "securityImpl");
  config->insert("security-client-auth-factory",
                 "createUserPasswordAuthInitInstance");
  switch (userType) {
    case 'W': {
      config->insert("security-username", "geode9");
      config->insert("security-password", "geode9");
      break;
    }
    case 'R': {
      char clientStr[32];
      sprintf(clientStr, "geode%d", clientNum);
      config->insert("security-username", clientStr);
      config->insert("security-password", clientStr);
      break;
    }
    case 'A': {
      config->insert("security-username", "geode1");
      config->insert("security-password", "geode1");
      break;
    }
    default: { break; }
  }
  initClient(true, config);
}

const char* getServerSecurityParams() {
  static std::string serverSecurityParams;

  if (serverSecurityParams.size() == 0) {
    serverSecurityParams =
        "security-client-authenticator="
        "javaobject.LdapUserAuthenticator.create "
        "security-client-accessor="
        "org.apache.geode.internal.security.FilterPreAuthorization.create "
        "security-client-accessor-pp="
        "org.apache.geode.internal.security.FilterPostAuthorization.create "
        "log-level=fine security-log-level=finest";

    char* ldapSrv = ACE_OS::getenv("LDAP_SERVER");
    serverSecurityParams += std::string(" security-ldap-server=") +
                            (ldapSrv != nullptr ? ldapSrv : "ldap");

    char* ldapRoot = ACE_OS::getenv("LDAP_BASEDN");
    serverSecurityParams +=
        std::string(" security-ldap-basedn=") +
        (ldapRoot != nullptr ? ldapRoot
                             : "ou=ldapTesting,dc=ldap,dc=apache,dc=org");

    char* ldapSSL = ACE_OS::getenv("LDAP_USESSL");
    serverSecurityParams += std::string(" security-ldap-usessl=") +
                            (ldapSSL != nullptr ? ldapSSL : "false");
  }
  return serverSecurityParams.c_str();
}

void getKeysVector(VectorOfCacheableKey& keysVec, int numKeys) {
  for (int index = 0; index < numKeys; ++index) {
    keysVec.push_back(CacheableString::create(keys[index]));
  }
}

void checkValuesMap(const HashMapOfCacheablePtr& values, int clientNum,
                    int numKeys) {
  int expectedNum = 0;
  CacheableKeyPtr key;
  CacheableStringPtr val;
  CacheableStringPtr expectedVal;
  for (int index = clientNum - 1; index < numKeys; index += clientNum) {
    ++expectedNum;
    key = CacheableString::create(keys[index]);
    const auto& iter = values->find(key);
    ASSERT(iter != values->end(), "key not found in values map");
    val = std::dynamic_pointer_cast<CacheableString>(iter->second);
    expectedVal = CacheableString::create(nvals[index]);
    ASSERT(*val == *expectedVal, "unexpected value in values map");
  }
  printf("Expected number of values: %d; got values: %zd", expectedNum,
         values->size());
  ASSERT(values->size() == expectedNum, "unexpected number of values");
}

void checkExceptionsMap(const HashMapOfExceptionPtr& exceptions, int clientNum,
                        int numKeys) {
  int expectedNum = 0;
  CacheableKeyPtr key;
  for (int index = 0; index < numKeys; ++index) {
    if ((index + 1) % clientNum != 0) {
      ++expectedNum;
      key = CacheableString::create(keys[index]);
      const auto& iter = exceptions->find(key);
      ASSERT(iter != exceptions->end(), "key not found in exceptions map");
      ASSERT(std::dynamic_pointer_cast<NotAuthorizedExceptionPtr>(iter->second),
             "unexpected exception type in exception map");
      printf("Got expected NotAuthorizedException: %s",
             iter->second->getMessage());
    }
  }
  printf("Expected number of exceptions: %d; got exceptions: %zd", expectedNum,
         exceptions->size());
  ASSERT(exceptions->size() == expectedNum, "unexpected number of exceptions");
}

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, StartLocator)
  {
    if (isLocator) CacheHelper::initLocator(1);
    LOG("Locator1 started");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, StartServer1)
  {
    if (isLocalServer) {
      CacheHelper::initServer(1, "cacheserver_notify_subscription.xml",
                              locHostPort, getServerSecurityParams());
      LOG("Server1 started");
    }
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, StartServer2)
  {
    if (isLocalServer) {
      CacheHelper::initServer(2, "cacheserver_notify_subscription2.xml",
                              locHostPort, getServerSecurityParams());
      LOG("Server2 started");
    }
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, StepOne)
  {
    initClientAuth('A');
    try {
      createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);
      createEntry(regionNamesAuth[0], keys[0], vals[0]);
      updateEntry(regionNamesAuth[0], keys[0], nvals[0]);
      invalidateEntry(regionNamesAuth[0], keys[0]);
      verifyInvalid(regionNamesAuth[0], keys[0]);
      destroyEntry(regionNamesAuth[0], keys[0]);
      verifyDestroyed(regionNamesAuth[0], keys[0]);
      destroyRegion(regionNamesAuth[0]);

      CacheableKeyPtr key0 = CacheableKey::create(keys[0]);
      CacheableStringPtr val0 = CacheableString::create(vals[0]);
      CacheableKeyPtr key2 = CacheableKey::create(keys[2]);
      CacheableStringPtr val2 = CacheableString::create(nvals[2]);

      createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);
      createEntry(regionNamesAuth[0], keys[0], vals[0]);
      createEntry(regionNamesAuth[0], keys[2], nvals[2]);
      RegionPtr regPtr0 = getHelper()->getRegion(regionNamesAuth[0]);
      if (regPtr0 != nullptr) {
        regPtr0->registerAllKeys();
        regPtr0->unregisterAllKeys();
      }

      VectorOfCacheableKey keysVec;
      keysVec.push_back(key0);
      keysVec.push_back(key2);
      auto values = std::make_shared<HashMapOfCacheable>();
      auto exceptions = std::make_shared<HashMapOfException>();
      regPtr0->getAll(keysVec, values, exceptions);
      ASSERT(values->size() == 2, "Expected 2 entries");
      ASSERT(exceptions->size() == (size_t)0, "Expected no exceptions");
      auto res0 = std::dynamic_pointer_cast<CacheableString>((*values)[key0]);
      auto res2 = std::dynamic_pointer_cast<CacheableString>((*values)[key2]);
      ASSERT(*res0 == *val0, "Unexpected value for key");
      ASSERT(*res2 == *val2, "Unexpected value for key");
    }
    HANDLE_NO_NOT_AUTHORIZED_EXCEPTION

    LOG("StepOne complete.");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(WRITER_CLIENT, StepTwo)
  {
    initClientAuth('W');
    try {
      createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);
      createEntry(regionNamesAuth[0], keys[0], vals[0]);
      updateEntry(regionNamesAuth[0], keys[0], nvals[0]);
      invalidateEntry(regionNamesAuth[0], keys[0]);
      verifyInvalid(regionNamesAuth[0], keys[0]);
      destroyEntry(regionNamesAuth[0], keys[0]);
      verifyDestroyed(regionNamesAuth[0], keys[0]);
      createEntry(regionNamesAuth[0], keys[0], vals[0]);
      updateEntry(regionNamesAuth[0], keys[0], nvals[0]);
      verifyEntry(regionNamesAuth[0], keys[0], nvals[0]);
      destroyRegion(regionNamesAuth[0]);
    }
    HANDLE_NO_NOT_AUTHORIZED_EXCEPTION

    try {
      createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);
      RegionPtr regPtr0 = getHelper()->getRegion(regionNamesAuth[0]);
      CacheableKeyPtr keyPtr = CacheableKey::create(keys[0]);
      auto checkPtr =
          std::dynamic_pointer_cast<CacheableString>(regPtr0->get(keyPtr));
      FAIL("Should get NotAuthorizedException");
    }
    HANDLE_NOT_AUTHORIZED_EXCEPTION

    try {
      createEntry(regionNamesAuth[0], keys[1], nvals[1]);
      createEntry(regionNamesAuth[0], keys[2], nvals[2]);
      createEntry(regionNamesAuth[0], keys[3], nvals[3]);
      createEntry(regionNamesAuth[0], keys[4], nvals[4]);
      createEntry(regionNamesAuth[0], keys[5], nvals[5]);
    }
    HANDLE_NO_NOT_AUTHORIZED_EXCEPTION

    LOG("StepTwo complete.");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(READER_CLIENT, StepThree)
  {
    initClientAuth('R', 2);
    createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);

    VectorOfCacheableKey keys;
    auto values = std::make_shared<HashMapOfCacheable>();
    auto exceptions = std::make_shared<HashMapOfException>();
    getKeysVector(keys, 6);
    RegionPtr rptr = getHelper()->getRegion(regionNamesAuth[0]);
    rptr->getAll(keys, values, exceptions);

    checkValuesMap(values, 2, 6);
    checkExceptionsMap(exceptions, 2, 6);

    LOG("StepThree complete.");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(READER2_CLIENT, StepFour)
  {
    initClientAuth('R', 3);
    createRegionForSecurity(regionNamesAuth[0], USE_ACK, true);

    VectorOfCacheableKey keys;
    auto values = std::make_shared<HashMapOfCacheable>();
    auto exceptions = std::make_shared<HashMapOfException>();
    getKeysVector(keys, 6);
    RegionPtr rptr = getHelper()->getRegion(regionNamesAuth[0]);
    rptr->getAll(keys, values, exceptions);

    checkValuesMap(values, 3, 6);
    checkExceptionsMap(exceptions, 3, 6);

    LOG("StepFour complete.");
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, CloseServer1)
  {
    if (isLocalServer) {
      CacheHelper::closeServer(1);
      LOG("Server1 stopped");
    }
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, CloseServer2)
  {
    if (isLocalServer) {
      CacheHelper::closeServer(2);
      LOG("Server2 stopped");
    }
  }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, CloseCacheAdmin)
  { cleanProc(); }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(WRITER_CLIENT, CloseCacheWriter)
  { cleanProc(); }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(READER_CLIENT, CloseCacheReader)
  { cleanProc(); }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(READER2_CLIENT, CloseCacheReader2)
  { cleanProc(); }
END_TASK_DEFINITION

DUNIT_TASK_DEFINITION(ADMIN_CLIENT, CloseLocator)
  {
    if (isLocator) {
      CacheHelper::closeLocator(1);
      LOG("Locator1 stopped");
    }
  }
END_TASK_DEFINITION

void doThinClientSecurityPostAuthorization() {
  CALL_TASK(StartLocator);
  CALL_TASK(StartServer1);
  CALL_TASK(StartServer2);
  CALL_TASK(StepOne);
  CALL_TASK(StepTwo);
  CALL_TASK(StepThree);
  CALL_TASK(StepFour);
  CALL_TASK(CloseServer1);
  CALL_TASK(CloseServer2);
  CALL_TASK(CloseCacheAdmin);
  CALL_TASK(CloseCacheWriter);
  CALL_TASK(CloseCacheReader);
  CALL_TASK(CloseCacheReader2);
  CALL_TASK(CloseLocator);
}

DUNIT_MAIN
  { doThinClientSecurityPostAuthorization(); }
END_MAIN
