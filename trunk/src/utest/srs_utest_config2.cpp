//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_config2.hpp>

#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_utest_kernel.hpp>

VOID TEST(ConfigMainTest, CheckIncludeEmptyConfig)
{
    srs_error_t err;

    if (true) {
        string filepath = _srs_tmp_file_prefix + "utest-main.conf";
        MockFileRemover _mfr(filepath);

        string included = _srs_tmp_file_prefix + "utest-included-empty.conf";
        MockFileRemover _mfr2(included);

        if (true) {
            SrsFileWriter fw;
            fw.open(included);
        }

        if (true) {
            SrsFileWriter fw;
            fw.open(filepath);
            string content = _MIN_OK_CONF "include " + included + ";";
            fw.write((void *)content.data(), (int)content.length(), NULL);
        }

        SrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse_file(filepath.c_str()));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }

    if (true) {
        MockSrsConfig conf;
        conf.mock_include("test.conf", "");
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "include test.conf;"));
        EXPECT_EQ(1, (int)conf.get_listens().size());
    }
}

VOID TEST(ConfigMainTest, CheckHttpListenFollow)
{
    srs_error_t err;

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}http_server{enabled on;listen 1985;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1985", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1985", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;listen 8080;https{enabled on;}}http_server{enabled on;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("8080", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("8080", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;listen 8080;https{enabled on;}}http_server{enabled on;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("8080", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("8080", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;https{enabled on;}}http_server{enabled on;listen 1985;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1985", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1985", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;listen 1234;https{enabled on;}}http_server{enabled on;listen 1234;https{enabled on;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1234", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1234", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("8088", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("8088", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }

    if (true) {
        MockSrsConfig conf;
        HELPER_ASSERT_SUCCESS(conf.parse(_MIN_OK_CONF "http_api{enabled on;listen 1234;https{enabled on;}}http_server{enabled on;listen 1234;https{enabled on;listen 4567;}}"));
        EXPECT_TRUE(conf.get_http_stream_enabled());

        // If http API use same port to HTTP server.
        EXPECT_EQ(1, (int)conf.get_http_stream_listens().size());
        EXPECT_STREQ("1234", conf.get_http_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_http_api_listens().size());
        EXPECT_STREQ("1234", conf.get_http_api_listens().at(0).c_str());

        // Then HTTPS API should use the same port to HTTPS server.
        EXPECT_EQ(1, (int)conf.get_https_stream_listens().size());
        EXPECT_STREQ("4567", conf.get_https_stream_listens().at(0).c_str());
        EXPECT_EQ(1, (int)conf.get_https_api_listens().size());
        EXPECT_STREQ("4567", conf.get_https_api_listens().at(0).c_str());
        EXPECT_TRUE(conf.get_http_stream_crossdomain());
    }
}
