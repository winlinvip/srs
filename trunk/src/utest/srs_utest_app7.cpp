//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_app7.hpp>

using namespace std;

#include <srs_app_rtc_conn.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_utest_app5.hpp>
#include <srs_utest_app6.hpp>

// Mock video recv track implementation
MockRtcVideoRecvTrackForNack::MockRtcVideoRecvTrackForNack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc)
    : SrsRtcVideoRecvTrack(receiver, track_desc)
{
    check_send_nacks_error_ = srs_success;
    check_send_nacks_count_ = 0;
}

MockRtcVideoRecvTrackForNack::~MockRtcVideoRecvTrackForNack()
{
}

srs_error_t MockRtcVideoRecvTrackForNack::check_send_nacks()
{
    check_send_nacks_count_++;
    return check_send_nacks_error_;
}

void MockRtcVideoRecvTrackForNack::set_check_send_nacks_error(srs_error_t err)
{
    check_send_nacks_error_ = err;
}

void MockRtcVideoRecvTrackForNack::reset()
{
    check_send_nacks_error_ = srs_success;
    check_send_nacks_count_ = 0;
}

// Mock audio recv track implementation
MockRtcAudioRecvTrackForNack::MockRtcAudioRecvTrackForNack(ISrsRtcPacketReceiver *receiver, SrsRtcTrackDescription *track_desc)
    : SrsRtcAudioRecvTrack(receiver, track_desc)
{
    check_send_nacks_error_ = srs_success;
    check_send_nacks_count_ = 0;
}

MockRtcAudioRecvTrackForNack::~MockRtcAudioRecvTrackForNack()
{
}

srs_error_t MockRtcAudioRecvTrackForNack::check_send_nacks()
{
    check_send_nacks_count_++;
    return check_send_nacks_error_;
}

void MockRtcAudioRecvTrackForNack::set_check_send_nacks_error(srs_error_t err)
{
    check_send_nacks_error_ = err;
}

void MockRtcAudioRecvTrackForNack::reset()
{
    check_send_nacks_error_ = srs_success;
    check_send_nacks_count_ = 0;
}

VOID TEST(SrsRtcPublishStreamTest, OnRtpCipherTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-rtp-cipher-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test typical RTP packet processing scenario
    // Create a simple RTP packet without extension (minimal valid packet)
    unsigned char simple_rtp_data[] = {
        // RTP header (12 bytes) - no extension
        0x80, 0x60, 0x12, 0x34, // V=2, P=0, X=0, CC=0, M=0, PT=96, seq=0x1234
        0x56, 0x78, 0x9A, 0xBC, // timestamp
        0xDE, 0xF0, 0x12, 0x34  // SSRC
    };

    // Test normal RTP packet processing (default state: no NACK simulation, no TWCC, no PT drop)
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtp_cipher((char *)simple_rtp_data, sizeof(simple_rtp_data)));

    // Test with NACK simulation enabled
    publish_stream->simulate_nack_drop(1); // Simulate dropping 1 packet
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtp_cipher((char *)simple_rtp_data, sizeof(simple_rtp_data)));

    // Test with a different RTP packet after NACK simulation is consumed
    unsigned char rtp_data2[] = {
        // RTP header (12 bytes) - different sequence number
        0x80, 0x60, 0x12, 0x35, // V=2, P=0, X=0, CC=0, M=0, PT=96, seq=0x1235
        0x56, 0x78, 0x9A, 0xBD, // timestamp
        0xDE, 0xF0, 0x12, 0x35  // SSRC
    };
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtp_cipher((char *)rtp_data2, sizeof(rtp_data2)));
}

VOID TEST(SrsRtcPublishStreamTest, OnRtpPlaintextTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-rtp-plaintext-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create video track with matching SSRC for the RTP packet
    SrsRtcTrackDescription video_desc;
    video_desc.type_ = "video";
    video_desc.id_ = "video_track_test";
    video_desc.ssrc_ = 0xDEF01234; // SSRC from RTP packet (0xDE, 0xF0, 0x12, 0x34)
    video_desc.is_active_ = true;
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, &video_desc);
    publish_stream->video_tracks_.push_back(video_track);

    // Enable tracks for processing
    publish_stream->set_all_tracks_status(true);

    // Test typical RTP plaintext packet processing scenario
    // Create a simple RTP packet without extension (minimal valid packet)
    unsigned char simple_rtp_data[] = {
        // RTP header (12 bytes) - no extension
        0x80, 0x60, 0x12, 0x34, // V=2, P=0, X=0, CC=0, M=0, PT=96, seq=0x1234
        0x56, 0x78, 0x9A, 0xBC, // timestamp
        0xDE, 0xF0, 0x12, 0x34  // SSRC = 0xDEF01234
    };

    // Test normal RTP plaintext packet processing
    // This tests the complete flow: packet wrapping, buffer creation, do_on_rtp_plaintext call, and cleanup
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtp_plaintext((char *)simple_rtp_data, sizeof(simple_rtp_data)));
}

VOID TEST(SrsRtcPublishStreamTest, CheckSendNacksTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-check-send-nacks-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test scenario 1: NACK disabled - should return success immediately
    publish_stream->nack_enabled_ = false;
    HELPER_EXPECT_SUCCESS(publish_stream->check_send_nacks());

    // Test scenario 2: NACK enabled with video and audio tracks
    publish_stream->nack_enabled_ = true;

    // Create mock video track
    SrsRtcTrackDescription video_desc;
    video_desc.type_ = "video";
    video_desc.id_ = "video_track_nack_test";
    video_desc.ssrc_ = 0x12345678;
    video_desc.is_active_ = true;
    MockRtcVideoRecvTrackForNack *video_track = new MockRtcVideoRecvTrackForNack(&mock_receiver, &video_desc);
    publish_stream->video_tracks_.push_back(video_track);

    // Create mock audio track
    SrsRtcTrackDescription audio_desc;
    audio_desc.type_ = "audio";
    audio_desc.id_ = "audio_track_nack_test";
    audio_desc.ssrc_ = 0x87654321;
    audio_desc.is_active_ = true;
    MockRtcAudioRecvTrackForNack *audio_track = new MockRtcAudioRecvTrackForNack(&mock_receiver, &audio_desc);
    publish_stream->audio_tracks_.push_back(audio_track);

    // Test successful NACK check for both tracks
    HELPER_EXPECT_SUCCESS(publish_stream->check_send_nacks());

    // Test video track error propagation
    video_track->set_check_send_nacks_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock video track error"));
    HELPER_EXPECT_FAILED(publish_stream->check_send_nacks());
    video_track->reset(); // Reset to success

    // Test audio track error propagation
    audio_track->set_check_send_nacks_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock audio track error"));
    HELPER_EXPECT_FAILED(publish_stream->check_send_nacks());
    audio_track->reset(); // Reset to success

    // Final test: both tracks successful again
    HELPER_EXPECT_SUCCESS(publish_stream->check_send_nacks());
}

// Helper function to create video track description with codec
SrsRtcTrackDescription *create_video_track_description_with_codec(std::string codec_name, uint32_t ssrc)
{
    SrsRtcTrackDescription *desc = new SrsRtcTrackDescription();
    desc->type_ = "video";
    desc->ssrc_ = ssrc;
    desc->id_ = "test-video-track";
    desc->is_active_ = true;
    desc->direction_ = "sendrecv";
    desc->mid_ = "0";

    // Create video payload with specified codec
    SrsVideoPayload *video_payload = new SrsVideoPayload(96, codec_name, 90000);
    desc->set_codec_payload(video_payload);

    return desc;
}

VOID TEST(SrsRtcPublishStreamTest, OnBeforeDecodePayloadTypicalScenario)
{
    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-on-before-decode-payload-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create video track with proper codec payload
    SrsUniquePtr<SrsRtcTrackDescription> video_desc(create_video_track_description_with_codec("H264", 0x12345678));
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, video_desc.get());
    publish_stream->video_tracks_.push_back(video_track);

    // Create audio track with proper codec payload
    SrsUniquePtr<SrsRtcTrackDescription> audio_desc(create_test_track_description("audio", 0x87654321));
    SrsRtcAudioRecvTrack *audio_track = new SrsRtcAudioRecvTrack(&mock_receiver, audio_desc.get());
    publish_stream->audio_tracks_.push_back(audio_track);

    // Test scenario 1: Empty buffer - should return early without processing
    SrsUniquePtr<SrsRtpPacket> pkt1(new SrsRtpPacket());
    pkt1->header_.set_ssrc(0x12345678); // Video track SSRC
    char empty_buffer_data[1024];
    SrsBuffer empty_buffer(empty_buffer_data, 0); // Empty buffer
    ISrsRtpPayloader *payload1 = NULL;
    SrsRtpPacketPayloadType ppt1 = SrsRtpPacketPayloadTypeUnknown;

    // Call on_before_decode_payload with empty buffer - should return without setting payload
    publish_stream->on_before_decode_payload(pkt1.get(), &empty_buffer, &payload1, &ppt1);
    EXPECT_TRUE(payload1 == NULL);                   // Should remain NULL for empty buffer
    EXPECT_EQ(SrsRtpPacketPayloadTypeUnknown, ppt1); // Should remain unknown

    // Test scenario 2: Video track processing with non-empty buffer
    SrsUniquePtr<SrsRtpPacket> pkt2(new SrsRtpPacket());
    pkt2->header_.set_ssrc(0x12345678); // Video track SSRC
    char video_buffer_data[1024];
    memset(video_buffer_data, 0x42, 100);           // Fill with test data
    SrsBuffer video_buffer(video_buffer_data, 100); // Non-empty buffer
    ISrsRtpPayloader *payload2 = NULL;
    SrsRtpPacketPayloadType ppt2 = SrsRtpPacketPayloadTypeUnknown;

    // Call on_before_decode_payload for video track - should delegate to video track
    publish_stream->on_before_decode_payload(pkt2.get(), &video_buffer, &payload2, &ppt2);
    // Video track should have processed the payload (implementation details depend on video track logic)

    // Test scenario 3: Audio track processing with non-empty buffer
    SrsUniquePtr<SrsRtpPacket> pkt3(new SrsRtpPacket());
    pkt3->header_.set_ssrc(0x87654321); // Audio track SSRC
    char audio_buffer_data[1024];
    memset(audio_buffer_data, 0x55, 80);           // Fill with test data
    SrsBuffer audio_buffer(audio_buffer_data, 80); // Non-empty buffer
    ISrsRtpPayloader *payload3 = NULL;
    SrsRtpPacketPayloadType ppt3 = SrsRtpPacketPayloadTypeUnknown;

    // Call on_before_decode_payload for audio track - should delegate to audio track
    publish_stream->on_before_decode_payload(pkt3.get(), &audio_buffer, &payload3, &ppt3);
    // Audio track should have processed the payload and set it to raw payload
    EXPECT_TRUE(payload3 != NULL);               // Audio track should create raw payload
    EXPECT_EQ(SrsRtpPacketPayloadTypeRaw, ppt3); // Audio track should set raw payload type

    // Test scenario 4: Unknown SSRC - should not match any track
    SrsUniquePtr<SrsRtpPacket> pkt4(new SrsRtpPacket());
    pkt4->header_.set_ssrc(0x99999999); // Unknown SSRC
    char unknown_buffer_data[1024];
    memset(unknown_buffer_data, 0x77, 50);             // Fill with test data
    SrsBuffer unknown_buffer(unknown_buffer_data, 50); // Non-empty buffer
    ISrsRtpPayloader *payload4 = NULL;
    SrsRtpPacketPayloadType ppt4 = SrsRtpPacketPayloadTypeUnknown;

    // Call on_before_decode_payload for unknown SSRC - should not process
    publish_stream->on_before_decode_payload(pkt4.get(), &unknown_buffer, &payload4, &ppt4);
    EXPECT_TRUE(payload4 == NULL);                   // Should remain NULL for unknown SSRC
    EXPECT_EQ(SrsRtpPacketPayloadTypeUnknown, ppt4); // Should remain unknown

    // Clean up payload created by audio track
    if (payload3) {
        srs_freep(payload3);
    }
}

VOID TEST(SrsRtcPublishStreamTest, SendPeriodicTwccTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-send-periodic-twcc-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test scenario 1: No TWCC feedback needed - should return success immediately
    HELPER_EXPECT_SUCCESS(publish_stream->send_periodic_twcc());

    // Test scenario 2: Add some TWCC packets to trigger feedback
    // First, add some packets to the TWCC receiver to make need_feedback() return true
    uint16_t test_sn1 = 1000;
    uint16_t test_sn2 = 1001;
    uint16_t test_sn3 = 1002;

    // Add packets to TWCC - this will make need_feedback() return true
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(test_sn1));
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(test_sn2));
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(test_sn3));

    // Now send_periodic_twcc should process the feedback and send RTCP packets
    HELPER_EXPECT_SUCCESS(publish_stream->send_periodic_twcc());

    // Verify that RTCP packets were sent through the mock receiver
    EXPECT_TRUE(mock_receiver.send_rtcp_count_ > 0);

    // Test scenario 3: Test with receiver send_rtcp error
    mock_receiver.set_send_rtcp_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock send rtcp error"));

    // Add more packets to trigger feedback again
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(1003));
    HELPER_EXPECT_SUCCESS(publish_stream->on_twcc(1004));

    // send_periodic_twcc should fail due to receiver error
    HELPER_EXPECT_FAILED(publish_stream->send_periodic_twcc());

    // Reset receiver error for cleanup
    mock_receiver.reset();
}

VOID TEST(SrsRtcPublishStreamTest, OnRtcpTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-on-rtcp-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test scenario 1: RTCP SR (Sender Report) - should call on_rtcp_sr
    SrsUniquePtr<SrsRtcpSR> sr(new SrsRtcpSR());
    sr->set_ssrc(0x12345678);
    sr->set_ntp(0x123456789ABCDEF0ULL);
    sr->set_rtp_ts(1000);
    sr->set_rtp_send_packets(100);
    sr->set_rtp_send_bytes(50000);
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtcp(sr.get()));

    // Test scenario 2: RTCP SDES - should be ignored and return success
    SrsUniquePtr<SrsRtcpCommon> sdes(new SrsRtcpCommon());
    sdes->header_.type = SrsRtcpType_sdes;
    sdes->set_ssrc(0xAABBCCDD);
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtcp(sdes.get()));

    // Test scenario 3: RTCP BYE - should be ignored and return success
    SrsUniquePtr<SrsRtcpCommon> bye(new SrsRtcpCommon());
    bye->header_.type = SrsRtcpType_bye;
    bye->set_ssrc(0xEEFF0011);
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtcp(bye.get()));

    // Test scenario 4: Unknown RTCP type - should return error
    SrsUniquePtr<SrsRtcpCommon> unknown(new SrsRtcpCommon());
    unknown->header_.type = 255; // Invalid/unknown RTCP type
    unknown->set_ssrc(0x99999999);
    HELPER_EXPECT_FAILED(publish_stream->on_rtcp(unknown.get()));
}

VOID TEST(SrsRtcPublishStreamTest, OnRtcpXrTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-on-rtcp-xr-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create video track to receive RTT updates
    SrsRtcTrackDescription video_desc;
    video_desc.type_ = "video";
    video_desc.id_ = "video_track_xr_test";
    video_desc.ssrc_ = 0x12345678;
    video_desc.is_active_ = true;
    SrsRtcVideoRecvTrack *video_track = new SrsRtcVideoRecvTrack(&mock_receiver, &video_desc);
    publish_stream->video_tracks_.push_back(video_track);

    // Create a valid RTCP XR packet with DLRR block (block type 5)
    // RTCP XR packet structure:
    // - RTCP header (8 bytes): V=2, P=0, RC=0, PT=207(XR), length, SSRC
    // - Report block: BT=5, reserved, block_length, SSRC, LRR, DLRR
    unsigned char xr_data[] = {
        // RTCP header (8 bytes)
        0x80, 0xCF, 0x00, 0x05, // V=2, P=0, RC=0, PT=207(XR), length=5 (24 bytes total)
        0x87, 0x65, 0x43, 0x21, // SSRC of XR packet sender
        // DLRR report block (16 bytes)
        0x05, 0x00, 0x00, 0x03, // BT=5 (DLRR), reserved=0, block_length=3 (12 bytes)
        0x12, 0x34, 0x56, 0x78, // SSRC of receiver (matches video track SSRC)
        0x12, 0x34, 0x56, 0x78, // LRR (Last Receiver Report) - 32-bit compact NTP
        0x00, 0x00, 0x10, 0x00  // DLRR (Delay since Last Receiver Report) - 32-bit value
    };

    // Create SrsRtcpXr object and set its data
    SrsUniquePtr<SrsRtcpXr> xr(new SrsRtcpXr());
    xr->set_ssrc(0x87654321);

    // Set the raw data for the XR packet (simulate what decode() would do)
    xr->data_ = (char *)xr_data;
    xr->nb_data_ = sizeof(xr_data);

    // Test RTCP XR processing - should parse DLRR block and update RTT
    HELPER_EXPECT_SUCCESS(publish_stream->on_rtcp_xr(xr.get()));

    // The function should have processed the DLRR block and called update_rtt
    // RTT calculation: compact_ntp - lrr - dlrr
    // This is a typical scenario where RTT is calculated from timing information
}

VOID TEST(SrsRtcPublishStreamTest, RequestKeyframeTypicalScenario)
{
    srs_error_t err;

    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-request-keyframe-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Test typical keyframe request scenario
    uint32_t test_ssrc = 0x12345678;
    SrsContextId subscriber_cid;
    subscriber_cid.set_value("test-subscriber-cid");

    // Test request_keyframe function - should delegate to pli_worker and log appropriately
    publish_stream->request_keyframe(test_ssrc, subscriber_cid);

    // Test do_request_keyframe function - should call receiver's send_rtcp_fb_pli
    HELPER_EXPECT_SUCCESS(publish_stream->do_request_keyframe(test_ssrc, subscriber_cid));

    // Verify that PLI packet was sent through the mock receiver
    EXPECT_EQ(1, mock_receiver.send_rtcp_fb_pli_count_);

    // Test error handling in do_request_keyframe
    mock_receiver.set_send_rtcp_fb_pli_error(srs_error_new(ERROR_RTC_RTP_MUXER, "mock PLI send error"));

    // Should still return success but log the error (error is freed internally)
    HELPER_EXPECT_SUCCESS(publish_stream->do_request_keyframe(test_ssrc, subscriber_cid));

    // Verify that PLI packet send was attempted again
    EXPECT_EQ(2, mock_receiver.send_rtcp_fb_pli_count_);

    // Reset receiver for cleanup
    mock_receiver.reset();
}

VOID TEST(SrsRtcPublishStreamTest, UpdateRttTypicalScenario)
{
    // Create mock objects
    MockRtcAsyncTaskExecutor mock_exec;
    MockRtcExpire mock_expire;
    MockRtcPacketReceiver mock_receiver;
    SrsContextId cid;
    cid.set_value("test-update-rtt-stream-id");

    // Create SrsRtcPublishStream with mock dependencies
    SrsUniquePtr<SrsRtcPublishStream> publish_stream(new SrsRtcPublishStream(&mock_exec, &mock_expire, &mock_receiver, cid));

    // Create audio track with specific SSRC
    SrsRtcTrackDescription audio_desc;
    audio_desc.type_ = "audio";
    audio_desc.id_ = "audio_track_rtt_test";
    audio_desc.ssrc_ = 0x87654321;
    audio_desc.is_active_ = true;
    SrsRtcAudioRecvTrack *audio_track = new SrsRtcAudioRecvTrack(&mock_receiver, &audio_desc);
    publish_stream->audio_tracks_.push_back(audio_track);

    // Test typical RTT update scenario for audio track
    uint32_t test_ssrc = 0x87654321; // Matches audio track SSRC
    int test_rtt = 50;               // 50ms RTT

    // Call update_rtt - should find audio track and update its RTT
    publish_stream->update_rtt(test_ssrc, test_rtt);

    // The function should have found the audio track and called update_rtt on it
    // This delegates to the NACK receiver which updates its RTT and nack_interval
    // No return value to check, but the function should complete without error

    // Test with unknown SSRC - should not find any track and return silently
    uint32_t unknown_ssrc = 0x99999999;
    publish_stream->update_rtt(unknown_ssrc, test_rtt);

    // Function should handle unknown SSRC gracefully without error
}
