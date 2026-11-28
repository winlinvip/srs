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
    config_ = NULL;
    rtc_sources_ = NULL;
#ifdef SRS_RTSP
    rtsp_sources_ = NULL;
#endif
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
        source->config_ = config_;
        source->rtc_sources_ = rtc_sources_;
#ifdef SRS_RTSP
        source->rtsp_sources_ = rtsp_sources_;
#endif
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
    StreamBridgesUtestStream()
    {
        rtmp_.config_ = &config_;
        rtmp_.rtc_sources_ = &rtc_;
#ifdef SRS_RTSP
        rtmp_.rtsp_sources_ = &rtsp_;
#endif
    }

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

// An RTMP encoder publishing live/livestream to the stream, through its connection. Its stream is its own, or the
// stream of another publisher.
class StreamBridgesUtestRtmpPublisher : public StreamBridgesUtestStream
{
public:
    StreamBridgesUtestStream *stream_;
    SrsRtmpConn *conn_;
    SrsSharedPtr<SrsRtmpSource> source_;

public:
    StreamBridgesUtestRtmpPublisher()
    {
        stream_ = this;
        conn_ = NULL;
    }
    StreamBridgesUtestRtmpPublisher(StreamBridgesUtestStream *stream)
    {
        stream_ = stream;
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
        conn_->config_ = &stream_->config_;
        conn_->assemble();
        conn_->rtmp_sources_ = &stream_->rtmp_;
        conn_->rtc_sources_ = &stream_->rtc_;
        conn_->srt_sources_ = &stream_->srt_;
        conn_->info_->req_->vhost_ = "__defaultVhost__";
        conn_->info_->req_->app_ = "live";
        conn_->info_->req_->stream_ = "livestream";
        conn_->info_->edge_ = stream_->config_.edge_;

        if ((err = stream_->rtmp_.fetch_or_create(conn_->info_->req_, source_)) != srs_success) {
            return srs_error_wrap(err, "rtmp source");
        }

        if (stream_->config_.edge_) {
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

// An SRT encoder publishing live/livestream to the stream, through its connection. Its stream is its own, or the
// stream of another publisher.
class StreamBridgesUtestSrtPublisher : public StreamBridgesUtestStream
{
public:
    StreamBridgesUtestStream *stream_;
    SrsMpegtsSrtConn *conn_;

public:
    StreamBridgesUtestSrtPublisher()
    {
        stream_ = this;
        conn_ = NULL;
    }
    StreamBridgesUtestSrtPublisher(StreamBridgesUtestStream *stream)
    {
        stream_ = stream;
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
        conn_->config_ = &stream_->config_;
        conn_->rtmp_sources_ = &stream_->rtmp_;
        conn_->rtc_sources_ = &stream_->rtc_;
        conn_->srt_sources_ = &stream_->srt_;
        conn_->req_->vhost_ = "__defaultVhost__";
        conn_->req_->app_ = "live";
        conn_->req_->stream_ = "livestream";

        if ((err = stream_->srt_.fetch_or_create(conn_->req_, conn_->srt_source_)) != srs_success) {
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
// source when its SDP is done, as SrsRtcConnection::add_publisher does, and the WHIP DELETE frees it. Its stream is
// its own, or the stream of another publisher.
class StreamBridgesUtestWhipPublisher : public StreamBridgesUtestStream
{
public:
    StreamBridgesUtestStream *stream_;
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
        stream_ = this;
        publisher_ = NULL;
    }
    StreamBridgesUtestWhipPublisher(StreamBridgesUtestStream *stream) : req_("__defaultVhost__", "live", "livestream")
    {
        stream_ = stream;
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

        SrsUniquePtr<SrsRtcSourceDescription> desc(tracks_.create_stream_description());
        SrsSharedPtr<SrsRtcSource> source;
        if ((err = stream_->rtc_.fetch_or_create(&req_, source)) != srs_success) {
            return srs_error_wrap(err, "rtc source");
        }
        if (!source->can_publish()) {
            return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req_.get_stream_url().c_str());
        }
        source->set_stream_created();
        source->set_stream_desc(desc.get());

        SrsContextId cid;
        cid.set_value("stream-bridges-whip");
        publisher_ = new SrsRtcPublishStream(&exec_, &expire_, &receiver_, cid);
        publisher_->config_ = &stream_->config_;
        publisher_->stat_ = &stat_;
        publisher_->rtc_sources_ = &stream_->rtc_;
        publisher_->rtmp_sources_ = &stream_->rtmp_;
        publisher_->srt_sources_ = &stream_->srt_;

        if ((err = publisher_->initialize(&req_, desc.get())) != srs_success) {
            srs_freep(publisher_);
            srs_freep(exec_.last_task_);
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

// The bridge table on an origin with every server and switch on. Each publisher claims the sources of the other
// protocols, and feeds them as the switches say: only RTMP feeds RTSP, SRT feeds WebRTC itself, as rtmp_to_rtc
// switches it, and no bridge feeds the publisher's own protocol.
VOID TEST(StreamBridgesTest, TableOnOriginWithEverySwitch)
{
    MockAppConfigForStreamBridges config;
    config.rtsp_enabled_ = true;
    config.rtsp_from_rtmp_ = true;
    config.rtc_from_rtmp_ = true;
    config.srt_to_rtmp_ = true;
    config.rtc_to_rtmp_ = true;

    SrsBridgeRow rtmp;
    rtmp.initialize(&config, SrsStreamProtocolRtmp, "__defaultVhost__");
    EXPECT_FALSE(rtmp.claim_rtmp_);
    EXPECT_TRUE(rtmp.claim_rtc_);
    EXPECT_TRUE(rtmp.claim_srt_);
    EXPECT_TRUE(rtmp.rtmp_to_rtc_);
#ifdef SRS_RTSP
    EXPECT_TRUE(rtmp.rtmp_to_rtsp_);
#else
    EXPECT_FALSE(rtmp.rtmp_to_rtsp_);
#endif
    EXPECT_FALSE(rtmp.srt_to_rtmp_);
    EXPECT_FALSE(rtmp.srt_to_rtc_);
    EXPECT_FALSE(rtmp.rtc_to_rtmp_);

    SrsBridgeRow srt;
    srt.initialize(&config, SrsStreamProtocolSrt, "__defaultVhost__");
    EXPECT_TRUE(srt.claim_rtmp_);
    EXPECT_TRUE(srt.claim_rtc_);
    EXPECT_FALSE(srt.claim_srt_);
    EXPECT_FALSE(srt.rtmp_to_rtc_);
    EXPECT_FALSE(srt.rtmp_to_rtsp_);
    EXPECT_TRUE(srt.srt_to_rtmp_);
    EXPECT_TRUE(srt.srt_to_rtc_);
    EXPECT_FALSE(srt.rtc_to_rtmp_);

    SrsBridgeRow rtc;
    rtc.initialize(&config, SrsStreamProtocolRtc, "__defaultVhost__");
    EXPECT_TRUE(rtc.claim_rtmp_);
    EXPECT_FALSE(rtc.claim_rtc_);
    EXPECT_TRUE(rtc.claim_srt_);
    EXPECT_FALSE(rtc.rtmp_to_rtc_);
    EXPECT_FALSE(rtc.rtmp_to_rtsp_);
    EXPECT_FALSE(rtc.srt_to_rtmp_);
    EXPECT_FALSE(rtc.srt_to_rtc_);
    EXPECT_TRUE(rtc.rtc_to_rtmp_);
}

// With every conversion off, a publisher feeds nothing, and still claims the sources of the other protocols.
VOID TEST(StreamBridgesTest, TableWithSwitchesOff)
{
    MockAppConfigForStreamBridges config;
    config.rtsp_enabled_ = true;

    SrsBridgeRow rtmp;
    rtmp.initialize(&config, SrsStreamProtocolRtmp, "__defaultVhost__");
    EXPECT_TRUE(rtmp.claim_rtc_);
    EXPECT_TRUE(rtmp.claim_srt_);
    EXPECT_FALSE(rtmp.rtmp_to_rtc_);
    EXPECT_FALSE(rtmp.rtmp_to_rtsp_);

    SrsBridgeRow srt;
    srt.initialize(&config, SrsStreamProtocolSrt, "__defaultVhost__");
    EXPECT_TRUE(srt.claim_rtmp_);
    EXPECT_TRUE(srt.claim_rtc_);
    EXPECT_FALSE(srt.srt_to_rtmp_);
    EXPECT_FALSE(srt.srt_to_rtc_);

    SrsBridgeRow rtc;
    rtc.initialize(&config, SrsStreamProtocolRtc, "__defaultVhost__");
    EXPECT_TRUE(rtc.claim_rtmp_);
    EXPECT_TRUE(rtc.claim_srt_);
    EXPECT_FALSE(rtc.rtc_to_rtmp_);
}

// With the WebRTC and SRT servers off, their sources have no publisher to refuse, so only the RTMP source is claimed,
// and rtmp_to_rtc feeds no WebRTC.
VOID TEST(StreamBridgesTest, TableWithServersOff)
{
    MockAppConfigForStreamBridges config;
    config.rtc_server_enabled_ = false;
    config.srt_enabled_ = false;
    config.rtc_from_rtmp_ = true;
    config.srt_to_rtmp_ = true;

    SrsBridgeRow rtmp;
    rtmp.initialize(&config, SrsStreamProtocolRtmp, "__defaultVhost__");
    EXPECT_FALSE(rtmp.claim_rtc_);
    EXPECT_FALSE(rtmp.claim_srt_);
    EXPECT_FALSE(rtmp.rtmp_to_rtc_);

    SrsBridgeRow srt;
    srt.initialize(&config, SrsStreamProtocolSrt, "__defaultVhost__");
    EXPECT_TRUE(srt.claim_rtmp_);
    EXPECT_FALSE(srt.claim_rtc_);
    EXPECT_TRUE(srt.srt_to_rtmp_);
    EXPECT_FALSE(srt.srt_to_rtc_);

    SrsBridgeRow rtc;
    rtc.initialize(&config, SrsStreamProtocolRtc, "__defaultVhost__");
    EXPECT_TRUE(rtc.claim_rtmp_);
    EXPECT_FALSE(rtc.claim_srt_);
}

// An edge vhost converts nothing, whatever the switches say. An RTMP publisher there goes to the origin and claims no
// other source; an SRT publisher claims the RTMP source; a WHIP publisher claims the RTMP and SRT sources.
VOID TEST(StreamBridgesTest, TableOnEdge)
{
    MockAppConfigForStreamBridges config;
    config.edge_ = true;
    config.rtsp_enabled_ = true;
    config.rtsp_from_rtmp_ = true;
    config.rtc_from_rtmp_ = true;
    config.srt_to_rtmp_ = true;
    config.rtc_to_rtmp_ = true;

    SrsBridgeRow rtmp;
    rtmp.initialize(&config, SrsStreamProtocolRtmp, "__defaultVhost__");
    EXPECT_FALSE(rtmp.claim_rtc_);
    EXPECT_FALSE(rtmp.claim_srt_);
    EXPECT_FALSE(rtmp.rtmp_to_rtc_);
    EXPECT_FALSE(rtmp.rtmp_to_rtsp_);

    SrsBridgeRow srt;
    srt.initialize(&config, SrsStreamProtocolSrt, "__defaultVhost__");
    EXPECT_TRUE(srt.claim_rtmp_);
    EXPECT_FALSE(srt.claim_rtc_);
    EXPECT_FALSE(srt.srt_to_rtmp_);
    EXPECT_FALSE(srt.srt_to_rtc_);

    SrsBridgeRow rtc;
    rtc.initialize(&config, SrsStreamProtocolRtc, "__defaultVhost__");
    EXPECT_TRUE(rtc.claim_rtmp_);
    EXPECT_TRUE(rtc.claim_srt_);
    EXPECT_FALSE(rtc.rtc_to_rtmp_);
}

// The RTMP source installs its own bridges when an RTMP publisher starts, with no connection to set them up: with
// rtmp_to_rtc and rtsp_from_rtmp, it feeds the WebRTC and RTSP sources.
VOID TEST(StreamBridgesTest, RtmpSourceInstallsBridgesOnPublish)
{
    srs_error_t err;

    StreamBridgesUtestStream s;
    s.config_.rtc_from_rtmp_ = true;
    s.config_.rtsp_enabled_ = true;
    s.config_.rtsp_from_rtmp_ = true;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    SrsSharedPtr<SrsRtmpSource> source;
    HELPER_EXPECT_SUCCESS(s.rtmp_.fetch_or_create(&req, source));

    HELPER_EXPECT_SUCCESS(source->on_publish());
    EXPECT_TRUE(s.rtmp_claimed());
#ifdef SRS_FFMPEG_FIT
    EXPECT_TRUE(s.rtc_claimed());
#endif
#ifdef SRS_RTSP
    EXPECT_TRUE(s.rtsp_claimed());
#endif

    source->on_unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.rtsp_claimed());
}

// When an SRT or WHIP publisher feeds the RTMP source, the RTMP source feeds no WebRTC or RTSP source: SRT feeds
// WebRTC itself, and no bridge loops back to WebRTC.
VOID TEST(StreamBridgesTest, RtmpSourceFedByBridgeInstallsNoBridge)
{
    srs_error_t err;

    StreamBridgesUtestStream s;
    s.config_.rtc_from_rtmp_ = true;
    s.config_.rtsp_enabled_ = true;
    s.config_.rtsp_from_rtmp_ = true;

    MockSrsRequest req("__defaultVhost__", "live", "livestream");
    SrsSharedPtr<SrsRtmpSource> source;
    HELPER_EXPECT_SUCCESS(s.rtmp_.fetch_or_create(&req, source));

    HELPER_EXPECT_SUCCESS(source->on_publish_by(SrsStreamProtocolSrt));
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.rtsp_claimed());
    source->on_unpublish();

    HELPER_EXPECT_SUCCESS(source->on_publish_by(SrsStreamProtocolRtc));
    EXPECT_TRUE(s.rtmp_claimed());
    EXPECT_FALSE(s.rtc_claimed());
    EXPECT_FALSE(s.rtsp_claimed());
    source->on_unpublish();
    EXPECT_FALSE(s.rtmp_claimed());
}

// Whether a publisher is refused because another protocol publishes the stream.
static bool stream_bridges_utest_busy(srs_error_t err)
{
    int code = srs_error_code(err);
    srs_freep(err);
    return code == ERROR_SYSTEM_STREAM_BUSY;
}

// An RTMP publisher has the stream, so an SRT publisher is refused and claims nothing, until the RTMP publisher stops.
VOID TEST(StreamBridgesTest, RtmpThenSrtIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher a;
    StreamBridgesUtestSrtPublisher b(&a);
    HELPER_EXPECT_SUCCESS(a.connect());
    HELPER_EXPECT_SUCCESS(b.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.rtmp_claimed());
    EXPECT_FALSE(a.srt_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.srt_claimed());
    b.unpublish();
    EXPECT_FALSE(a.srt_claimed());
}

// An RTMP publisher has the stream, so a WHIP publisher is refused and claims nothing, until the RTMP publisher stops.
VOID TEST(StreamBridgesTest, RtmpThenWhipIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestRtmpPublisher a;
    StreamBridgesUtestWhipPublisher b(&a);
    HELPER_EXPECT_SUCCESS(a.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.rtmp_claimed());
    EXPECT_FALSE(a.rtc_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.rtc_claimed());
    b.unpublish();
    EXPECT_FALSE(a.rtc_claimed());
}

// An SRT publisher has the stream, so an RTMP publisher is refused and claims nothing, until the SRT publisher stops.
VOID TEST(StreamBridgesTest, SrtThenRtmpIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher a;
    StreamBridgesUtestRtmpPublisher b(&a);
    HELPER_EXPECT_SUCCESS(a.connect());
    HELPER_EXPECT_SUCCESS(b.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.srt_claimed());
    EXPECT_FALSE(a.rtmp_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.rtmp_claimed());
    b.unpublish();
    EXPECT_FALSE(a.rtmp_claimed());
}

// An SRT publisher has the stream, so a WHIP publisher is refused and claims nothing, until the SRT publisher stops.
VOID TEST(StreamBridgesTest, SrtThenWhipIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestSrtPublisher a;
    StreamBridgesUtestWhipPublisher b(&a);
    HELPER_EXPECT_SUCCESS(a.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.srt_claimed());
    EXPECT_FALSE(a.rtc_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.rtc_claimed());
    b.unpublish();
    EXPECT_FALSE(a.rtc_claimed());
}

// A WHIP publisher has the stream, so an RTMP publisher is refused and claims nothing, until the WHIP publisher stops.
VOID TEST(StreamBridgesTest, WhipThenRtmpIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher a;
    StreamBridgesUtestRtmpPublisher b(&a);
    HELPER_EXPECT_SUCCESS(b.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.rtc_claimed());
    EXPECT_FALSE(a.rtmp_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.rtmp_claimed());
    b.unpublish();
    EXPECT_FALSE(a.rtmp_claimed());
}

// A WHIP publisher has the stream, so an SRT publisher is refused and claims nothing, until the WHIP publisher stops.
VOID TEST(StreamBridgesTest, WhipThenSrtIsBusy)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher a;
    StreamBridgesUtestSrtPublisher b(&a);
    HELPER_EXPECT_SUCCESS(b.connect());

    HELPER_EXPECT_SUCCESS(a.publish());
    EXPECT_TRUE(stream_bridges_utest_busy(b.publish()));
    EXPECT_TRUE(a.rtc_claimed());
    EXPECT_FALSE(a.srt_claimed());

    a.unpublish();
    HELPER_EXPECT_SUCCESS(b.publish());
    EXPECT_TRUE(a.srt_claimed());
    b.unpublish();
    EXPECT_FALSE(a.srt_claimed());
}

// A WHIP publisher has the WebRTC source, so an SRT publisher that would feed RTMP and WebRTC is refused, and claims
// neither the SRT source nor the RTMP source it would feed; an RTMP publisher with rtmp_to_rtc is refused the same.
VOID TEST(StreamBridgesTest, FailedClaimLeavesNothingClaimed)
{
    srs_error_t err;

    StreamBridgesUtestWhipPublisher a;
    a.config_.srt_to_rtmp_ = true;
    a.config_.rtc_from_rtmp_ = true;
    StreamBridgesUtestSrtPublisher srt(&a);
    StreamBridgesUtestRtmpPublisher rtmp(&a);
    HELPER_EXPECT_SUCCESS(srt.connect());
    HELPER_EXPECT_SUCCESS(rtmp.connect());

    HELPER_EXPECT_SUCCESS(a.publish());

    EXPECT_TRUE(stream_bridges_utest_busy(srt.publish()));
    EXPECT_FALSE(a.srt_claimed());
    EXPECT_FALSE(a.rtmp_claimed());

    EXPECT_TRUE(stream_bridges_utest_busy(rtmp.publish()));
    EXPECT_FALSE(a.rtmp_claimed());
    EXPECT_TRUE(a.rtc_claimed());

    a.unpublish();
    EXPECT_FALSE(a.rtc_claimed());
}
