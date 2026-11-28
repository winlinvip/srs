//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai53.hpp>

#include <srs_app_edge.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_srt_conn.hpp>
#include <srs_kernel_error.hpp>
#include <srs_utest_ai15.hpp>
#include <srs_utest_ai38.hpp>

MockAppConfigForStreamBridges::MockAppConfigForStreamBridges()
{
    edge_ = false;
    rtsp_enabled_ = false;
    rtsp_from_rtmp_ = false;

    rtc_server_enabled_ = true;
    rtc_enabled_ = true;
    srt_enabled_ = true;
    rtc_from_rtmp_ = false;
    srt_to_rtmp_ = false;
    rtc_to_rtmp_ = false;
}

MockAppConfigForStreamBridges::~MockAppConfigForStreamBridges()
{
}

bool MockAppConfigForStreamBridges::get_vhost_is_edge(std::string vhost)
{
    return edge_;
}

bool MockAppConfigForStreamBridges::get_rtsp_server_enabled()
{
    return rtsp_enabled_;
}

bool MockAppConfigForStreamBridges::get_rtsp_enabled(std::string vhost)
{
    return rtsp_enabled_;
}

bool MockAppConfigForStreamBridges::get_rtsp_from_rtmp(std::string vhost)
{
    return rtsp_from_rtmp_;
}

MockRtmpSourcesForStreamBridges::MockRtmpSourcesForStreamBridges()
{
}

MockRtmpSourcesForStreamBridges::~MockRtmpSourcesForStreamBridges()
{
}

srs_error_t MockRtmpSourcesForStreamBridges::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtmpSource> &pps)
{
    srs_error_t err = srs_success;

    if (!source_.get()) {
        SrsRtmpSource *source = new SrsRtmpSource();
        source->handler_ = &handler_;
        SrsSharedPtr<SrsRtmpSource> wrapper(source);
        source->assemble();
        if ((err = source->initialize(wrapper, r)) != srs_success) {
            return srs_error_wrap(err, "rtmp source");
        }
        source_ = wrapper;
    }

    pps = source_;
    return err;
}

SrsSharedPtr<SrsRtmpSource> MockRtmpSourcesForStreamBridges::fetch(ISrsRequest *r)
{
    return source_;
}

void MockRtmpSourcesForStreamBridges::dispose()
{
}

srs_error_t MockRtmpSourcesForStreamBridges::initialize()
{
    return srs_success;
}

MockRtcSourcesForStreamBridges::MockRtcSourcesForStreamBridges()
{
}

MockRtcSourcesForStreamBridges::~MockRtcSourcesForStreamBridges()
{
}

srs_error_t MockRtcSourcesForStreamBridges::initialize()
{
    return srs_success;
}

srs_error_t MockRtcSourcesForStreamBridges::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps)
{
    srs_error_t err = srs_success;

    if (!source_.get()) {
        SrsSharedPtr<SrsRtcSource> source(new SrsRtcSource());
        source->assemble();
        if ((err = source->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtc source");
        }
        source_ = source;
    }

    pps = source_;
    return err;
}

SrsSharedPtr<SrsRtcSource> MockRtcSourcesForStreamBridges::fetch(ISrsRequest *r)
{
    return source_;
}

MockSrtSourcesForStreamBridges::MockSrtSourcesForStreamBridges()
{
}

MockSrtSourcesForStreamBridges::~MockSrtSourcesForStreamBridges()
{
}

srs_error_t MockSrtSourcesForStreamBridges::initialize()
{
    return srs_success;
}

srs_error_t MockSrtSourcesForStreamBridges::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps)
{
    srs_error_t err = srs_success;

    if (!source_.get()) {
        SrsSharedPtr<SrsSrtSource> source(new SrsSrtSource());
        source->assemble();
        if ((err = source->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "srt source");
        }
        source_ = source;
    }

    pps = source_;
    return err;
}

SrsSharedPtr<SrsSrtSource> MockSrtSourcesForStreamBridges::fetch(ISrsRequest *r)
{
    return source_;
}

#ifdef SRS_RTSP
MockRtspSourcesForStreamBridges::MockRtspSourcesForStreamBridges()
{
}

MockRtspSourcesForStreamBridges::~MockRtspSourcesForStreamBridges()
{
}

srs_error_t MockRtspSourcesForStreamBridges::initialize()
{
    return srs_success;
}

srs_error_t MockRtspSourcesForStreamBridges::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps)
{
    srs_error_t err = srs_success;

    if (!source_.get()) {
        SrsSharedPtr<SrsRtspSource> source(new SrsRtspSource());
        source->assemble();
        if ((err = source->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtsp source");
        }
        source_ = source;
    }

    pps = source_;
    return err;
}

SrsSharedPtr<SrsRtspSource> MockRtspSourcesForStreamBridges::fetch(ISrsRequest *r)
{
    return source_;
}
#endif

// One stream, live/livestream, with a source per protocol, which a publisher of one protocol claims when it starts,
// and the sources it feeds by a bridge with it: a claimed source refuses another publisher, until the publisher stops.
class StreamBridgesUtestStream
{
public:
    MockAppConfigForStreamBridges config_;
    MockRtmpSourcesForStreamBridges rtmp_;
    MockRtcSourcesForStreamBridges rtc_;
    MockSrtSourcesForStreamBridges srt_;
#ifdef SRS_RTSP
    MockRtspSourcesForStreamBridges rtsp_;
#endif

public:
    // Whether the RTMP source refuses another publisher, on an origin or on an edge.
    bool rtmp_claimed()
    {
        return rtmp_.source_.get() && !rtmp_.source_->can_publish(config_.edge_);
    }
    bool rtc_claimed()
    {
        return rtc_.source_.get() && !rtc_.source_->can_publish();
    }
    bool srt_claimed()
    {
        return srt_.source_.get() && !srt_.source_->can_publish();
    }
    bool rtsp_claimed()
    {
#ifdef SRS_RTSP
        return rtsp_.source_.get() && !rtsp_.source_->can_publish();
#else
        return false;
#endif
    }
};

// An RTMP encoder publishing live/livestream to the stream, through its connection.
class StreamBridgesUtestRtmpPublisher : public StreamBridgesUtestStream
{
public:
    SrsRtmpConn *conn_;
    SrsSharedPtr<SrsRtmpSource> source_;

public:
    StreamBridgesUtestRtmpPublisher()
    {
        conn_ = NULL;
    }
    virtual ~StreamBridgesUtestRtmpPublisher()
    {
        srs_freep(conn_);
    }

public:
    // The encoder connects and asks to publish; on an edge, the source pushes to the origin by a mock.
    srs_error_t connect()
    {
        srs_error_t err = srs_success;

        conn_ = new SrsRtmpConn(new MockRtmpTransportForDoCycle(), "192.168.1.100", 1935);
        conn_->config_ = &config_;
        conn_->assemble();
        conn_->rtc_sources_ = &rtc_;
        conn_->srt_sources_ = &srt_;
#ifdef SRS_RTSP
        conn_->rtsp_sources_ = &rtsp_;
#endif
        conn_->info_->req_->vhost_ = "__defaultVhost__";
        conn_->info_->req_->app_ = "live";
        conn_->info_->req_->stream_ = "livestream";
        conn_->info_->edge_ = config_.edge_;

        if ((err = rtmp_.fetch_or_create(conn_->info_->req_, source_)) != srs_success) {
            return srs_error_wrap(err, "rtmp source");
        }

        if (config_.edge_) {
            srs_freep(source_->publish_edge_->forwarder_);
            source_->publish_edge_->forwarder_ = new MockEdgeForwarderForRtmpSource();
        }

        return err;
    }
    srs_error_t publish()
    {
        return conn_->acquire_publish(source_);
    }
    void unpublish()
    {
        conn_->release_publish(source_);
    }
};

// With rtmp_to_rtc, an RTMP publisher feeds the WebRTC source, which its players read; it feeds no SRT or RTSP
// source.
VOID TEST(StreamBridgesTest, RtmpPublisherFeedsWebrtcWithRtmpToRtc)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher s;
    s.config_.rtc_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_TRUE(s.rtc_claimed());
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtsp_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
}

// Without rtmp_to_rtc, an RTMP publisher feeds only its own source, so a WHIP publisher may still claim the WebRTC
// source.
VOID TEST(StreamBridgesTest, RtmpPublisherFeedsNoWebrtcWithoutRtmpToRtc)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher s;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtsp_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
}

#ifdef SRS_RTSP
// With rtsp_from_rtmp on an RTSP server, an RTMP publisher feeds the RTSP source.
VOID TEST(StreamBridgesTest, RtmpPublisherFeedsRtspWithRtspFromRtmp)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher s;
    s.config_.rtsp_enabled_ = true;
    s.config_.rtsp_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_TRUE(s.rtsp_claimed());
    EXPECT_FALSE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtsp_claimed());
}
#endif

// On an edge, an RTMP publisher's stream goes to the origin, and no conversion is made, whatever the switches say.
VOID TEST(StreamBridgesTest, RtmpPublisherOnEdgeFeedsNothing)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher s;
    s.config_.edge_ = true;
    s.config_.rtc_from_rtmp_ = true;
    s.config_.rtsp_enabled_ = true;
    s.config_.rtsp_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtsp_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
}

// An SRT encoder publishing live/livestream to the stream, through its connection.
class StreamBridgesUtestSrtPublisher : public StreamBridgesUtestStream
{
public:
    SrsMpegtsSrtConn *conn_;

public:
    StreamBridgesUtestSrtPublisher()
    {
        conn_ = NULL;
    }
    virtual ~StreamBridgesUtestSrtPublisher()
    {
        srs_freep(conn_);
    }

public:
    srs_error_t connect()
    {
        srs_error_t err = srs_success;

        conn_ = new SrsMpegtsSrtConn(NULL, 1, "192.168.1.100", 9000);
        conn_->config_ = &config_;
        conn_->rtmp_sources_ = &rtmp_;
        conn_->rtc_sources_ = &rtc_;
        conn_->srt_sources_ = &srt_;
        conn_->req_->vhost_ = "__defaultVhost__";
        conn_->req_->app_ = "live";
        conn_->req_->stream_ = "livestream";

        if ((err = srt_.fetch_or_create(conn_->req_, conn_->srt_source_)) != srs_success) {
            return srs_error_wrap(err, "srt source");
        }

        return err;
    }
    srs_error_t publish()
    {
        return conn_->acquire_publish();
    }
    void unpublish()
    {
        conn_->release_publish();
    }
};

// With srt_to_rtmp and rtmp_to_rtc, an SRT publisher feeds the RTMP source and the WebRTC source.
VOID TEST(StreamBridgesTest, SrtPublisherFeedsRtmpAndWebrtc)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher s;
    s.config_.srt_to_rtmp_ = true;
    s.config_.rtc_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.srt_claimed());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_TRUE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
}

// With srt_to_rtmp only, an SRT publisher feeds the RTMP source, and not the WebRTC source.
VOID TEST(StreamBridgesTest, SrtPublisherFeedsRtmpWithSrtToRtmp)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher s;
    s.config_.srt_to_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.srt_claimed());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
}

// rtmp_to_rtc switches SRT to WebRTC too, which SRS converts directly, so it works with srt_to_rtmp off, and the RTMP
// source stays free.
VOID TEST(StreamBridgesTest, SrtPublisherFeedsWebrtcWithoutSrtToRtmp)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher s;
    s.config_.rtc_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.srt_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_TRUE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.srt_claimed());
    EXPECT_FALSE(s.rtc_claimed());
}

// With both switches off, an SRT publisher feeds only its own source.
VOID TEST(StreamBridgesTest, SrtPublisherFeedsNothingWithSwitchesOff)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher s;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.srt_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.srt_claimed());
}

// On an edge vhost, an SRT publisher feeds nothing, whatever the switches say.
VOID TEST(StreamBridgesTest, SrtPublisherOnEdgeFeedsNothing)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher s;
    s.config_.edge_ = true;
    s.config_.srt_to_rtmp_ = true;
    s.config_.rtc_from_rtmp_ = true;
    HELPER_EXPECT_SUCCESS(s.connect());

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.srt_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());

    s.unpublish();
    EXPECT_FALSE(s.srt_claimed());
}

// A WHIP publisher of Opus and H.264 publishing live/livestream to the stream: its publish stream claims the WebRTC
// source once DTLS is done, and the WHIP DELETE frees it.
class StreamBridgesUtestWhipPublisher : public StreamBridgesUtestStream
{
public:
    MockAppStatistic stat_;
    MockRtcAsyncCallRequest req_;
    MockRtcAsyncTaskExecutor exec_;
    MockExpire expire_;
    MockRtcPacketReceiver receiver_;
    MockRtcTrackDescriptionFactory tracks_;
    SrsRtcPublishStream *publisher_;

public:
    StreamBridgesUtestWhipPublisher() : req_("__defaultVhost__", "live", "livestream")
    {
        publisher_ = NULL;
    }
    virtual ~StreamBridgesUtestWhipPublisher()
    {
        unpublish();
    }

public:
    srs_error_t publish()
    {
        srs_error_t err = srs_success;

        SrsContextId cid;
        cid.set_value("stream-bridges-whip");
        publisher_ = new SrsRtcPublishStream(&exec_, &expire_, &receiver_, cid);
        publisher_->config_ = &config_;
        publisher_->stat_ = &stat_;
        publisher_->rtc_sources_ = &rtc_;
        publisher_->rtmp_sources_ = &rtmp_;
        publisher_->srt_sources_ = &srt_;

        SrsUniquePtr<SrsRtcSourceDescription> desc(tracks_.create_stream_description());
        if ((err = rtc_.fetch_or_create(&req_, publisher_->source_)) != srs_success) {
            return srs_error_wrap(err, "rtc source");
        }
        rtc_.source_->set_stream_desc(desc.get());

        if ((err = publisher_->initialize(&req_, desc.get())) != srs_success) {
            return srs_error_wrap(err, "initialize");
        }

        return publisher_->start();
    }
    void unpublish()
    {
        if (publisher_) {
            publisher_->stop();
        }
        srs_freep(publisher_);
        srs_freep(exec_.last_task_);
    }
};

// With rtc_to_rtmp, a WHIP publisher feeds the RTMP source, and no SRT source.
VOID TEST(StreamBridgesTest, WhipPublisherFeedsRtmpWithRtcToRtmp)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher s;
    s.config_.rtc_to_rtmp_ = true;

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtc_claimed());
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.srt_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
}

// Without rtc_to_rtmp, a WHIP publisher feeds only its own source.
VOID TEST(StreamBridgesTest, WhipPublisherFeedsNoRtmpWithoutRtcToRtmp)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher s;

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtc_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.srt_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtc_claimed());
}

// On an edge vhost, a WHIP publisher feeds nothing, even with rtc_to_rtmp.
VOID TEST(StreamBridgesTest, WhipPublisherOnEdgeFeedsNothing)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher s;
    s.config_.edge_ = true;
    s.config_.rtc_to_rtmp_ = true;

    HELPER_EXPECT_SUCCESS(s.publish());
    EXPECT_TRUE(s.rtc_claimed());
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.srt_claimed());

    s.unpublish();
    EXPECT_FALSE(s.rtc_claimed());
}
