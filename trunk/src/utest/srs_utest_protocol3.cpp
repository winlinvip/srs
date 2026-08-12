//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_protocol3.hpp>

using namespace std;

#include <srs_kernel_error.hpp>
#include <srs_core_autofree.hpp>
#include <srs_protocol_utility.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_amf0.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_protocol_http_conn.hpp>
#include <srs_protocol_protobuf.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_app_rtc_sdp.hpp>

extern bool srs_is_valid_jsonp_callback(std::string callback);

VOID TEST(ProtocolHttpTest, JsonpCallbackName)
{
    EXPECT_TRUE(srs_is_valid_jsonp_callback(""));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("callback"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback-1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback_1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback.1234567890"));
    EXPECT_TRUE(srs_is_valid_jsonp_callback("Callback1234567890-_."));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback()//"));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback!"));
    EXPECT_FALSE(srs_is_valid_jsonp_callback("callback;"));
}

VOID TEST(ProtocolSdpTest, SrsSSRCGroupEncode)
{
    srs_error_t err = srs_success;

    vector<uint32_t> ssrcs;
    ssrcs.push_back(12345);
    ssrcs.push_back(67890);

    SrsSSRCGroup ssrc_group("FID", ssrcs);
    ostringstream os;
    HELPER_EXPECT_SUCCESS(ssrc_group.encode(os));

    EXPECT_STREQ("a=ssrc-group:FID 12345 67890\r\n", os.str().c_str());
}

VOID TEST(ProtocolSdpTest, SrsSSRCGroupEncodeBeforeSsrcInfo)
{
    srs_error_t err = srs_success;

    vector<uint32_t> ssrcs;
    ssrcs.push_back(12345);
    ssrcs.push_back(67890);

    SrsSSRCGroup ssrc_group("FID", ssrcs);
    SrsSSRCInfo ssrc_info(12345, "test-cname", "", "");

    ostringstream os;
    HELPER_EXPECT_SUCCESS(ssrc_group.encode(os));
    HELPER_EXPECT_SUCCESS(ssrc_info.encode(os));

    EXPECT_STREQ("a=ssrc-group:FID 12345 67890\r\n"
        "a=ssrc:12345 cname:test-cname\r\n", os.str().c_str());
}
