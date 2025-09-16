//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_protocol4.hpp>

using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_protocol_http_client.hpp>
#include <srs_protocol_conn.hpp>
#include <srs_protocol_st.hpp>
#include <srs_app_st.hpp>
#include <srs_utest_http.hpp>

VOID TEST(HTTPSClientTest, HTTPSClientPost)
{
    srs_error_t err;

    // Test HTTPS POST request
    if (true) {
        SrsHttpsTestServer server("HTTPS OK");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(100 * 1000); // 100ms

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        string post_data = "{\"test\": \"data\"}";
        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.post("/api/test", post_data, &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(8, nn);
        EXPECT_STREQ("HTTPS OK", buf);
    }

    // Test HTTPS GET request
    if (true) {
        SrsHttpsTestServer server("HTTPS OK");
        HELPER_ASSERT_SUCCESS(server.start());

        // Give server time to start
        srs_usleep(100 * 1000); // 100ms

        SrsHttpClient client;
        HELPER_ASSERT_SUCCESS(client.initialize("https", "127.0.0.1", server.get_port(), 5 * SRS_UTIME_SECONDS));

        ISrsHttpMessage *res = NULL;
        HELPER_ASSERT_SUCCESS(client.get("/api/test", "", &res));
        SrsUniquePtr<ISrsHttpMessage> res_uptr(res);

        ISrsHttpResponseReader *br = res->body_reader();
        ASSERT_FALSE(br->eof());

        ssize_t nn = 0;
        char buf[1024];
        HELPER_ARRAY_INIT(buf, sizeof(buf), 0);
        HELPER_ASSERT_SUCCESS(br->read(buf, sizeof(buf), &nn));
        ASSERT_EQ(8, nn);
        EXPECT_STREQ("HTTPS OK", buf);
    }
}
