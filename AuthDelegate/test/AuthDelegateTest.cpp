/*
 * AuthDelegateTest.cpp
 *
 * Copyright 2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */


#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AuthDelegate/AuthDelegate.h"
#include "AuthDelegate/MockConfig.h"
#include "AuthDelegate/MockHttpPost.h"
#include "AVSUtils/Initialization/AlexaClientSDKInit.h"
#include "MockAuthObserver.h"

using namespace alexaClientSDK::authDelegate;
using namespace alexaClientSDK::acl;
using ResponseCode = HttpPostInterface::ResponseCode;

using namespace ::testing;

/// URL to which the refresh token and access token request should be sent.
static const std::string DEFAULT_LWA_URL = "https://api.amazon.com/auth/o2/token";

/**
 * Time out for AuthDelegateObserver to wait for a state change. Wait for 60 seconds to make sure AuthDelegate has
 * enough time to notify the change of state or fail quickly enough on timeout.
 */
static const auto TIME_OUT_IN_SECONDS = std::chrono::seconds(60);

/**
 * 'invalid_request' Error Code from LWA (@see https://images-na.ssl-images-amazon.com/images/G/01/lwa/dev/
 * docs/website-developer-guide._TTH_.pdf).
 */
static const std::string ERROR_CODE_INVALID_REQUEST = "invalid_request";


/// Define test fixture for testing AuthDelegate.
class AuthDelegateTest : public ::testing::Test {
protected:

    /// Initialize the objects for testing
    AuthDelegateTest() {
        m_mockHttpPost = std::unique_ptr<MockHttpPost>(new MockHttpPost());
        m_mockConfig = std::make_shared<NiceMock<MockConfig>>();
        m_mockAuthObserver = std::make_shared<NiceMock<MockAuthObserver>>();
    }

    /// Stub certain mock objects with default actions
    virtual void SetUp() override {

        ASSERT_TRUE(alexaClientSDK::avsUtils::initialization::AlexaClientSDKInit::initialize());

        ON_CALL(*m_mockHttpPost, doPost(_, _, _, _)).WillByDefault(Return(ResponseCode::UNDEFINED));
        ON_CALL(*m_mockConfig, getClientId()).WillByDefault(Return("testClientId (invalid)"));
        ON_CALL(*m_mockConfig, getClientSecret()).WillByDefault(Return("testClientSecret (invalid)"));
        ON_CALL(*m_mockConfig, getRefreshToken()).WillByDefault(Return("testRefreshToken (invalid)"));
        ON_CALL(*m_mockConfig, getLwaUrl()).WillByDefault(Return(DEFAULT_LWA_URL));
    }

    virtual void TearDown() override {
        alexaClientSDK::avsUtils::initialization::AlexaClientSDKInit::uninitialize();
    }

    /**
     * Wait on a condition for the specified duration.
     *
     * @param seconds Specify how many seconds to wait on a condition before timeout.
     * @param predicate The condition to wait on until it becomes true.
     * @return false if timed out, true otherwise.
     */
    bool waitFor(std::chrono::seconds seconds, std::function<bool()> predicate) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cv.wait_for(lock, seconds, predicate);
    }

    /**
     * Generate a valid LWA response with specified expiration duration in seconds.
     *
     * @param seconds Specify in how many seconds the token would become expire.
     * @return the a valid JSON string response that has the expected expiration.
     */
    std::string generateValidLwaResponseWithExpiration(std::chrono::seconds seconds) {
        std::string response = R"({
                    "access_token":"Atza|IQEBLjAsAhQ3yD47Jkj09BfU_qgNk4",
                    "expires_in":)";
        response += std::to_string(seconds.count());
        response += R"(,
                     "refresh_token":"Atzr|IQEBLzAtAhUAibmh-1N0EVztZJofMx",
                     "token_type":"bearer"
                })";
        return response;
    }

    /**
     * Generate a error LWA response with specified error code.
     *
     * @param errorCode the error code in LWA's response.
     * @return the JSON string that contains error code and descriptions.
     */
    std::string generateErrorLwaResponseWithErrorCode(const std::string& errorCode) {
        std::string response = R"({
                    "error":")";
        response += errorCode;
        response += R"(",
                    "error_description":"invalid request",
                    "request_id":"test_ID")";
        return response;
    }

    /// Mock object of @c HttpPostInterface through which refresh token request is sent in AuthDelegate.
    std::unique_ptr<MockHttpPost> m_mockHttpPost;

    /// Mock object of @c Config which provides the AuthDelegate required information to get access tokens from LWA.
    std::shared_ptr<NiceMock<MockConfig>> m_mockConfig;

    /// Mock object of @c AuthObserverInterface which will be notified on current AuthDelegate status.
    std::shared_ptr<NiceMock<MockAuthObserver>> m_mockAuthObserver;

    /// Condition variable used by @c waitFor function.
    std::condition_variable m_cv;

    /// Mutex used with condition variable @c m_cv.
    std::mutex m_mutex;
};

/**
 * Test create() with a @c nullptr Config, expecting @c nullptr to be returned.
 */
TEST_F(AuthDelegateTest, createNullConfig) {
    ASSERT_FALSE(AuthDelegate::create(nullptr, std::move(m_mockHttpPost)));
}

/**
 * Test create() without a clientId set, expecting a @c nullptr to be returned.
 */
TEST_F(AuthDelegateTest, createMissingClientId) {

    EXPECT_CALL(*m_mockConfig, getClientId()).WillRepeatedly(Return(""));

    ASSERT_FALSE(AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost)));
}

/**
 * Test create() without a clientSecret set, expecting a @c nullptr to be returned.
 */
TEST_F(AuthDelegateTest, createMissingClientSecret) {

    EXPECT_CALL(*m_mockConfig, getClientSecret()).WillRepeatedly(Return(""));

    ASSERT_FALSE(AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost)));
}

/**
 * Test create() without a refresh token set, expecting a @c nullptr to be returned.
 */
TEST_F(AuthDelegateTest, createMissingRefreshToken) {

    EXPECT_CALL(*m_mockConfig, getRefreshToken()).WillRepeatedly(Return(""));

    ASSERT_FALSE(AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost)));
}

/**
 * Test create() with a valid Config, expecting a valid AuthDelegate to be returned.
 */
TEST_F(AuthDelegateTest, create) {
    ASSERT_TRUE(AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost)));
}

/**
 * Test setAuthObserver() with a @c nullptr, expecting no exceptions or crashes.
 */
TEST_F(AuthDelegateTest, setAuthObserverNull) {
    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    ASSERT_TRUE(authDelegate);

    authDelegate->setAuthObserver(nullptr);

    SUCCEED();
}

/**
 * Test setAuthObserver() with a valid observer, expecting the observer to be updated with an UNINITIALIZED
 * state.
 */
TEST_F(AuthDelegateTest, setAuthObserver) {

    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    ASSERT_TRUE(authDelegate);

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNINITIALIZED));
    authDelegate->setAuthObserver(m_mockAuthObserver);
}

/**
 * Test retry logic of AuthDelegate.
 *
 * The initial AuthObserver should be in state UNINITIALIZED when there is no
 * valid response. After getting valid response from "server", the status state should be changed to REFRESHED.
 */
TEST_F(AuthDelegateTest, retry) {
    bool tokenRefreshed = false;
    const auto& validResponse = generateValidLwaResponseWithExpiration(std::chrono::seconds(60));
    EXPECT_CALL(*m_mockHttpPost, doPost(_, _, _, _))
            .WillOnce(Return(ResponseCode::UNDEFINED))
            .WillOnce(Return(ResponseCode::UNDEFINED))
            .WillOnce(DoAll(SetArgReferee<3>(validResponse), Return(ResponseCode::SUCCESS_OK)));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNINITIALIZED))
            .Times(AtMost(1));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::REFRESHED))
            .WillOnce(InvokeWithoutArgs([this, &tokenRefreshed]() {
                tokenRefreshed = true;
                m_cv.notify_all();
            }));

    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    authDelegate->setAuthObserver(m_mockAuthObserver);
    waitFor(TIME_OUT_IN_SECONDS, [&tokenRefreshed]() { return tokenRefreshed;});
}

/**
 * Test expiration notification from AuthDelegate
 *
 * When access token expired before the earliest time AuthDelegate can send a refresh token request,
 * the observer of AuthDelegate should be notified of the token expiration.
 */
TEST_F(AuthDelegateTest, expirationNotification) {
    bool tokenExpired = false;
    const auto& validResponse = generateValidLwaResponseWithExpiration(std::chrono::seconds(1));

    EXPECT_CALL(*m_mockHttpPost, doPost(_, _, _, _))
            .WillOnce(DoAll(SetArgReferee<3>(validResponse), Return(ResponseCode::SUCCESS_OK)))
            .WillRepeatedly(Return(ResponseCode::UNDEFINED));

    ::testing::InSequence s;
    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNINITIALIZED))
            .Times(AtMost(1));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::REFRESHED))
            .Times(1);

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::EXPIRED))
            .WillOnce(InvokeWithoutArgs([this, &tokenExpired]() {
                tokenExpired = true;
                m_cv.notify_all();
            }));

    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    authDelegate->setAuthObserver(m_mockAuthObserver);
    waitFor(TIME_OUT_IN_SECONDS, [&tokenExpired]() {return tokenExpired;});
}

/**
 * Test AuthDelegate can recover after token expiration
 *
 * After a token expiration, AuthDelegate should be able to recover to REFRESHED state after getting a valid
 * token from LWA
 */
TEST_F(AuthDelegateTest, recoverAfterExpiration) {
    bool tokenRefreshed = false;
    const auto& validResponse = generateValidLwaResponseWithExpiration(std::chrono::seconds(1));

    EXPECT_CALL(*m_mockHttpPost, doPost(_, _, _, _))
            .WillOnce(DoAll(SetArgReferee<3>(validResponse), Return(ResponseCode::SUCCESS_OK)))
            .WillOnce(Return(ResponseCode::UNDEFINED))
            .WillOnce(Return(ResponseCode::UNDEFINED))
            .WillOnce(DoAll(SetArgReferee<3>(validResponse), Return(ResponseCode::SUCCESS_OK)));

    ::testing::InSequence s;
    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNINITIALIZED))
            .Times(AtMost(1));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::REFRESHED))
            .Times(1);

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::EXPIRED))
            .Times(1);

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::REFRESHED))
            .WillOnce(InvokeWithoutArgs([this, &tokenRefreshed]() {
                tokenRefreshed = true;
                m_cv.notify_all();
            }));

    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    authDelegate->setAuthObserver(m_mockAuthObserver);
    waitFor(TIME_OUT_IN_SECONDS, [&tokenRefreshed]() {return tokenRefreshed;});
}

/**
 * Test AuthDelegate will notify the observer of the UNRECOVERABLE_ERROR
 *
 * After sending a invalid request to LWA, LWA should send us an invalid_request error and the AuthObserver should be
 * notified of the UNRECOVERABLE_ERROR state.
 */
TEST_F(AuthDelegateTest, unrecoverableErrorNotification) {
    bool errorReceived = false;
    const auto& invalidRequestResponse = generateErrorLwaResponseWithErrorCode(ERROR_CODE_INVALID_REQUEST);

    EXPECT_CALL(*m_mockHttpPost, doPost(_, _, _, _))
            .WillOnce(DoAll(SetArgReferee<3>(invalidRequestResponse), Return(ResponseCode::CLIENT_ERROR_BAD_REQUEST)));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNINITIALIZED))
            .Times(AtMost(1));

    EXPECT_CALL(*m_mockAuthObserver, onAuthStateChange(AuthObserverInterface::State::UNRECOVERABLE_ERROR))
            .WillOnce(InvokeWithoutArgs([this, &errorReceived]() {
                errorReceived = true;
                m_cv.notify_all();
            }));

    auto authDelegate = AuthDelegate::create(m_mockConfig, std::move(m_mockHttpPost));
    authDelegate->setAuthObserver(m_mockAuthObserver);
    waitFor(TIME_OUT_IN_SECONDS, [&errorReceived]() {return errorReceived;});
}