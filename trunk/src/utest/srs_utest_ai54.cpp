//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest_ai54.hpp>

#include <srs_kernel_error.hpp>
#include <srs_protocol_rtmp_msg_array.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#include <srs_utest_manual_coworkers.hpp>

#include <string.h>

using namespace std;

MockAppConfigForHttpSource::MockAppConfigForHttpSource()
{
    http_remux_ = true;
    reduce_sequence_header_ = false;
}

MockAppConfigForHttpSource::~MockAppConfigForHttpSource()
{
}

bool MockAppConfigForHttpSource::get_vhost_http_remux_enabled(std::string vhost)
{
    return http_remux_;
}

bool MockAppConfigForHttpSource::get_reduce_sequence_header(std::string vhost)
{
    return reduce_sequence_header_;
}

MockHttpSourceHandler::MockHttpSourceHandler()
{
    nb_mounts_ = 0;
    nb_unmounts_ = 0;
}

MockHttpSourceHandler::~MockHttpSourceHandler()
{
}

srs_error_t MockHttpSourceHandler::http_mount(ISrsRequest *r)
{
    nb_mounts_++;
    return srs_success;
}

void MockHttpSourceHandler::http_unmount(ISrsRequest *r)
{
    nb_unmounts_++;
}

MockEdgePullForHttpPlayers::MockEdgePullForHttpPlayers()
{
    nb_starts_ = 0;
    nb_stops_ = 0;
}

MockEdgePullForHttpPlayers::~MockEdgePullForHttpPlayers()
{
}

srs_error_t MockEdgePullForHttpPlayers::initialize(SrsSharedPtr<SrsRtmpSource> s, ISrsPlayEdge *e, ISrsRequest *r)
{
    return srs_success;
}

srs_error_t MockEdgePullForHttpPlayers::start()
{
    nb_starts_++;
    return srs_success;
}

void MockEdgePullForHttpPlayers::stop()
{
    nb_stops_++;
}

void mock_edge_pull_for_http_players(SrsRtmpSource *source, MockEdgePullForHttpPlayers *pull)
{
    SrsPlayEdge *edge = source->play_edge_;
    srs_freep(edge->ingester_);
    edge->ingester_ = pull;
}

// The media of an encoder as FLV tags: H.264 and AAC.
static const uint8_t hsource_avc_sh[] = {
    0x17, 0x00, 0x00, 0x00, 0x00, 0x01, 0x64, 0x00, 0x20, 0xff, 0xe1, 0x00, 0x19, 0x67, 0x64, 0x00, 0x20, 0xac,
    0xd9, 0x40, 0xc0, 0x29, 0xb0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32, 0x0f, 0x18,
    0x31, 0x96, 0x01, 0x00, 0x05, 0x68, 0xeb, 0xec, 0xb2, 0x2c};
static const uint8_t hsource_avc_idr[] = {0x17, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x65, 0x88, 0x84, 0x00, 0x10};
static const uint8_t hsource_aac_sh[] = {0xaf, 0x00, 0x12, 0x10};
static const uint8_t hsource_aac_frame[] = {0xaf, 0x01, 0x21, 0x00};

// One stream, live/livestream, published to its RTMP source, which feeds the HTTP source of the stream that the HTTP
// players read.
class HttpSourceUtestStream
{
public:
    MockAppConfigForHttpSource config_;
    MockRtmpSourcesForStreamBridges rtmp_;
    MockRtcSourcesForStreamBridges rtc_;
    SrsHttpSourceManager http_;
    MockHttpSourceHandler handler_;
    MockSrsRequest req_;
    SrsSharedPtr<SrsRtmpSource> rtmp_source_;
    SrsSharedPtr<SrsHttpSource> http_source_;

public:
    HttpSourceUtestStream() : req_("__defaultVhost__", "live", "livestream")
    {
        rtmp_.config_ = &config_;
        rtmp_.rtc_sources_ = &rtc_;
    }
    virtual ~HttpSourceUtestStream()
    {
        if (rtmp_source_.get()) {
            rtmp_source_->on_unpublish();
        }
    }

public:
    srs_error_t initialize()
    {
        srs_error_t err = srs_success;

        if ((err = rtmp_.fetch_or_create(&req_, rtmp_source_)) != srs_success) {
            return srs_error_wrap(err, "rtmp source");
        }
        rtmp_source_->http_sources_ = &http_;

        if ((err = http_.fetch_or_create(&req_, http_source_)) != srs_success) {
            return srs_error_wrap(err, "http source");
        }
        http_source_->config_ = &config_;
        http_source_->handler_ = &handler_;

        return err;
    }
    // The encoder's message, as the RTMP connection passes it to the RTMP source: 'a' audio, 'v' video, 'm' metadata.
    srs_error_t send(char type, uint32_t timestamp, const uint8_t *data = NULL, int size = 0)
    {
        SrsRtmpCommonMessage msg;
        if (type == 'm') {
            SrsOnMetaDataPacket meta;
            meta.metadata_->set("width", SrsAmf0Any::number(768));
            msg.header_.initialize_amf0_script(0, 1);
            msg.header_.timestamp_ = timestamp;
            return rtmp_source_->on_meta_data(&msg, &meta);
        }

        if (type == 'a') {
            msg.header_.initialize_audio(size, timestamp, 1);
        } else {
            msg.header_.initialize_video(size, timestamp, 1);
        }
        msg.create_payload(size);
        memcpy(msg.payload(), data, size);
        return (type == 'a') ? rtmp_source_->on_audio(&msg) : rtmp_source_->on_video(&msg);
    }
    // The metadata, the sequence headers and one GOP of an encoder.
    srs_error_t send_gop()
    {
        srs_error_t err = srs_success;
        if ((err = send('m', 0)) != srs_success) {
            return err;
        }
        if ((err = send('a', 0, hsource_aac_sh, sizeof(hsource_aac_sh))) != srs_success) {
            return err;
        }
        if ((err = send('v', 0, hsource_avc_sh, sizeof(hsource_avc_sh))) != srs_success) {
            return err;
        }
        if ((err = send('v', 40, hsource_avc_idr, sizeof(hsource_avc_idr))) != srs_success) {
            return err;
        }
        return send('a', 46, hsource_aac_frame, sizeof(hsource_aac_frame));
    }
};

// The messages a player has in its queue, as a string of their types: 'm' metadata, 'a' audio, 'v' video, and 'A'
// or 'V' for a sequence header.
static string hsource_dump(ISrsMediaConsumer *consumer)
{
    SrsMessageArray msgs(32);
    int count = 0;
    srs_error_t err = consumer->dump_packets(&msgs, count);
    EXPECT_TRUE(err == srs_success) << srs_error_desc(err);
    srs_freep(err);

    string types;
    for (int i = 0; i < count; i++) {
        SrsMediaPacket *msg = msgs.msgs_[i];
        if (msg->is_audio()) {
            types += (msg->payload()[1] == 0x00) ? "A" : "a";
        } else if (msg->is_video()) {
            types += (msg->payload()[1] == 0x00) ? "V" : "v";
        } else {
            types += "m";
        }
    }
    msgs.free(count);
    return types;
}

// With http_remux on, the RTMP source feeds the HTTP source whoever publishes, on an origin or an edge, since an HTTP
// player plays the RTMP stream: an HTTP stream is no conversion, and an edge serves its HTTP players too.
VOID TEST(HttpSourceTest, TableFeedsHttpWhereHttpRemuxIsOn)
{
    MockAppConfigForHttpSource config;
    SrsStreamProtocol protocols[] = {SrsStreamProtocolRtmp, SrsStreamProtocolSrt, SrsStreamProtocolRtc};

    for (int edge = 0; edge < 2; edge++) {
        config.edge_ = (edge == 1);
        for (int i = 0; i < 3; i++) {
            SrsBridgeRow row;
            row.initialize(&config, protocols[i], "__defaultVhost__");
            EXPECT_TRUE(row.rtmp_to_http_) << "protocol " << i << ", edge " << edge;
        }
    }

    config.http_remux_ = false;
    for (int edge = 0; edge < 2; edge++) {
        config.edge_ = (edge == 1);
        for (int i = 0; i < 3; i++) {
            SrsBridgeRow row;
            row.initialize(&config, protocols[i], "__defaultVhost__");
            EXPECT_FALSE(row.rtmp_to_http_) << "protocol " << i << ", edge " << edge;
        }
    }
}

// The HTTP stream is mounted while the RTMP source publishes, whether an RTMP encoder or a bridge from SRT or WebRTC
// feeds it, and unmounted when it stops.
VOID TEST(HttpSourceTest, HttpStreamMountedWhilePublishing)
{
    srs_error_t err;

    SrsStreamProtocol protocols[] = {SrsStreamProtocolRtmp, SrsStreamProtocolSrt, SrsStreamProtocolRtc};
    for (int i = 0; i < 3; i++) {
        HttpSourceUtestStream s;
        HELPER_EXPECT_SUCCESS(s.initialize());
        EXPECT_TRUE(s.http_source_->can_publish_);

        HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish_by(protocols[i]));
        EXPECT_EQ(1, s.handler_.nb_mounts_) << "protocol " << i;
        EXPECT_FALSE(s.http_source_->can_publish_) << "protocol " << i;

        s.rtmp_source_->on_unpublish();
        EXPECT_EQ(1, s.handler_.nb_unmounts_) << "protocol " << i;
        EXPECT_TRUE(s.http_source_->can_publish_) << "protocol " << i;
    }
}

// Without http_remux, the stream has no HTTP stream to mount.
VOID TEST(HttpSourceTest, NoHttpStreamWithoutHttpRemux)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    s.config_.http_remux_ = false;
    HELPER_EXPECT_SUCCESS(s.initialize());

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish());
    EXPECT_EQ(0, s.handler_.nb_mounts_);
    EXPECT_TRUE(s.http_source_->can_publish_);

    HELPER_EXPECT_SUCCESS(s.send_gop());
    EXPECT_TRUE(s.http_source_->meta_->data() == NULL);
}

// An HTTP player that waits before the encoder gets the whole stream from the HTTP source, and a late player gets the
// metadata, the sequence headers and the GOP from its caches, as an RTMP player gets them from the RTMP source.
VOID TEST(HttpSourceTest, HttpPlayersGetTheStream)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    SrsHttpConsumer *early_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(early_raw));
    SrsUniquePtr<SrsHttpConsumer> early(early_raw);
    HELPER_EXPECT_SUCCESS(s.http_source_->consumer_dumps(early.get()));

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish());
    HELPER_EXPECT_SUCCESS(s.send_gop());
    EXPECT_STREQ("mAVva", hsource_dump(early.get()).c_str());

    SrsHttpConsumer *late_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(late_raw));
    SrsUniquePtr<SrsHttpConsumer> late(late_raw);
    HELPER_EXPECT_SUCCESS(s.http_source_->consumer_dumps(late.get()));
    EXPECT_STREQ("mAVva", hsource_dump(late.get()).c_str());

    // The AAC and MP3 players have caches of their own, so they skip the GOP.
    SrsHttpConsumer *audio_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(audio_raw));
    SrsUniquePtr<SrsHttpConsumer> audio(audio_raw);
    HELPER_EXPECT_SUCCESS(s.http_source_->consumer_dumps(audio.get(), true, true, false));
    EXPECT_STREQ("mAV", hsource_dump(audio.get()).c_str());
}

// With reduce_sequence_header, a sequence header that repeats the last one reaches no HTTP player, as no RTMP player.
VOID TEST(HttpSourceTest, RepeatedSequenceHeaderDropped)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    s.config_.reduce_sequence_header_ = true;
    HELPER_EXPECT_SUCCESS(s.initialize());

    SrsHttpConsumer *consumer_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(consumer_raw));
    SrsUniquePtr<SrsHttpConsumer> consumer(consumer_raw);

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish());
    HELPER_EXPECT_SUCCESS(s.send_gop());
    HELPER_EXPECT_SUCCESS(s.send('v', 80, hsource_avc_sh, sizeof(hsource_avc_sh)));
    HELPER_EXPECT_SUCCESS(s.send('v', 80, hsource_avc_idr, sizeof(hsource_avc_idr)));
    EXPECT_STREQ("mAVvav", hsource_dump(consumer.get()).c_str());
}

// The HTTP source caches a GOP as the RTMP source does: a WebRTC publisher turns the RTMP source's GOP cache off, to
// keep the streams in sync, so a late HTTP player gets only the sequence headers; a player that turns it on again
// turns it on for both.
VOID TEST(HttpSourceTest, GopCacheFollowsRtmpSource)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    s.rtmp_source_->set_cache(false);
    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish_by(SrsStreamProtocolRtc));
    HELPER_EXPECT_SUCCESS(s.send_gop());

    SrsHttpConsumer *late_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(late_raw));
    SrsUniquePtr<SrsHttpConsumer> late(late_raw);
    HELPER_EXPECT_SUCCESS(s.http_source_->consumer_dumps(late.get()));
    EXPECT_STREQ("mAV", hsource_dump(late.get()).c_str());

    s.rtmp_source_->set_cache(true);
    s.rtmp_source_->set_gop_cache_max_frames(100);
    EXPECT_TRUE(s.http_source_->gop_cache_->enabled());
    EXPECT_EQ(100, s.http_source_->gop_cache_->gop_cache_max_frames());
}

// When the encoder stops, the GOP is gone, and a player that comes after gets nothing old.
VOID TEST(HttpSourceTest, UnpublishClearsTheStream)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish());
    HELPER_EXPECT_SUCCESS(s.send_gop());
    s.rtmp_source_->on_unpublish();

    SrsHttpConsumer *consumer_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(consumer_raw));
    SrsUniquePtr<SrsHttpConsumer> consumer(consumer_raw);
    HELPER_EXPECT_SUCCESS(s.http_source_->consumer_dumps(consumer.get()));
    EXPECT_STREQ("", hsource_dump(consumer.get()).c_str());
}

// An HTTP player is a player of the stream, so the publisher is not idle while one plays, and the RTMP source stays
// alive for a player that waits for the encoder.
VOID TEST(HttpSourceTest, HttpPlayerIsPlayerOfRtmpSource)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_publish());
    EXPECT_NE(0, s.rtmp_source_->publisher_idle_at_);

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    EXPECT_EQ(0, s.rtmp_source_->publisher_idle_at_);
    EXPECT_TRUE(s.rtmp_source_->has_players());

    s.rtmp_source_->on_http_stop();
    EXPECT_NE(0, s.rtmp_source_->publisher_idle_at_);
    EXPECT_FALSE(s.rtmp_source_->has_players());

    s.rtmp_source_->on_unpublish();
    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    s.rtmp_source_->stream_die_at_ = 0;
    EXPECT_FALSE(s.rtmp_source_->stream_is_dead());
    s.rtmp_source_->on_http_stop();
    s.rtmp_source_->stream_die_at_ = 0;
    EXPECT_TRUE(s.rtmp_source_->stream_is_dead());
}

// The HTTP source of a stream nobody publishes or plays is freed after a while, as the sources of the other protocols.
VOID TEST(HttpSourceTest, IdleHttpSourceCleanedUp)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    SrsHttpConsumer *consumer_raw = NULL;
    HELPER_EXPECT_SUCCESS(s.http_source_->create_consumer(consumer_raw));
    s.http_source_->stream_die_at_ = 0;
    HELPER_EXPECT_SUCCESS(s.http_.notify(0, 0, 0));
    EXPECT_TRUE(s.http_.fetch(&s.req_).get() != NULL);

    srs_freep(consumer_raw);
    s.http_source_->stream_die_at_ = 0;
    HELPER_EXPECT_SUCCESS(s.http_.notify(0, 0, 0));
    EXPECT_TRUE(s.http_.fetch(&s.req_).get() == NULL);
}

// On an edge, the stream is pulled from the origin only while someone plays it. The first HTTP player starts the pull,
// a second one joins it, and the pull stops when the last one leaves; the next player starts it again.
VOID TEST(HttpSourceTest, EdgePullFollowsHttpPlayers)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    s.config_.edge_ = true;
    HELPER_EXPECT_SUCCESS(s.initialize());

    MockEdgePullForHttpPlayers *pull = new MockEdgePullForHttpPlayers();
    mock_edge_pull_for_http_players(s.rtmp_source_.get(), pull);

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    EXPECT_EQ(1, pull->nb_starts_);
    EXPECT_EQ(0, pull->nb_stops_);

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    EXPECT_EQ(1, pull->nb_starts_);

    s.rtmp_source_->on_http_stop();
    EXPECT_EQ(0, pull->nb_stops_);

    s.rtmp_source_->on_http_stop();
    EXPECT_EQ(1, pull->nb_stops_);
    EXPECT_FALSE(s.rtmp_source_->has_players());

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    EXPECT_EQ(2, pull->nb_starts_);
    s.rtmp_source_->on_http_stop();
    EXPECT_EQ(2, pull->nb_stops_);
}

// RTMP and HTTP players of an edge stream share one pull: whichever comes first starts it, and it stops only when the
// last player leaves, whichever protocol that one plays.
VOID TEST(HttpSourceTest, EdgePullSharedByRtmpAndHttpPlayers)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    s.config_.edge_ = true;
    HELPER_EXPECT_SUCCESS(s.initialize());

    MockEdgePullForHttpPlayers *pull = new MockEdgePullForHttpPlayers();
    mock_edge_pull_for_http_players(s.rtmp_source_.get(), pull);

    // An HTTP player first, then an RTMP player; the HTTP player leaves first.
    if (true) {
        HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
        EXPECT_EQ(1, pull->nb_starts_);

        SrsRtmpConsumer *consumer_raw = NULL;
        HELPER_EXPECT_SUCCESS(s.rtmp_source_->create_consumer(consumer_raw));
        SrsUniquePtr<SrsRtmpConsumer> consumer(consumer_raw);
        EXPECT_EQ(1, pull->nb_starts_);

        s.rtmp_source_->on_http_stop();
        EXPECT_EQ(0, pull->nb_stops_);
    }
    EXPECT_EQ(1, pull->nb_stops_);

    // An RTMP player first, then an HTTP player; the RTMP player leaves first.
    if (true) {
        SrsRtmpConsumer *consumer_raw = NULL;
        HELPER_EXPECT_SUCCESS(s.rtmp_source_->create_consumer(consumer_raw));
        EXPECT_EQ(2, pull->nb_starts_);

        HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
        EXPECT_EQ(2, pull->nb_starts_);

        srs_freep(consumer_raw);
        EXPECT_EQ(1, pull->nb_stops_);

        s.rtmp_source_->on_http_stop();
        EXPECT_EQ(2, pull->nb_stops_);
    }
    EXPECT_FALSE(s.rtmp_source_->has_players());
}

// An origin has no pull: its HTTP players never start one.
VOID TEST(HttpSourceTest, OriginHttpPlayersPullNothing)
{
    srs_error_t err;

    HttpSourceUtestStream s;
    HELPER_EXPECT_SUCCESS(s.initialize());

    MockEdgePullForHttpPlayers *pull = new MockEdgePullForHttpPlayers();
    mock_edge_pull_for_http_players(s.rtmp_source_.get(), pull);

    HELPER_EXPECT_SUCCESS(s.rtmp_source_->on_http_play());
    s.rtmp_source_->on_http_stop();
    EXPECT_EQ(0, pull->nb_starts_);
    EXPECT_EQ(0, pull->nb_stops_);
}
