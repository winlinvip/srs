//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_SOURCE_HPP
#define SRS_APP_HTTP_SOURCE_HPP

#include <srs_core.hpp>

#include <map>
#include <string>
#include <vector>

#include <srs_app_rtmp_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_protocol_st.hpp>

class ISrsRequest;
class ISrsAppConfig;
class ISrsAppFactory;
class ISrsClock;
class SrsMediaPacket;
class SrsHttpConsumer;

// The server implements it, to mount the HTTP stream when the HTTP source starts publishing.
class ISrsHttpSourceHandler
{
public:
    ISrsHttpSourceHandler();
    virtual ~ISrsHttpSourceHandler();

public:
    virtual srs_error_t http_mount(ISrsRequest *r) = 0;
    virtual void http_unmount(ISrsRequest *r) = 0;
};

class ISrsHttpSource
{
public:
    ISrsHttpSource();
    virtual ~ISrsHttpSource();

public:
    virtual void on_consumer_destroy(SrsHttpConsumer *consumer) = 0;
};

// A player of the HTTP source, such as an HTTP-FLV, HTTP-TS, HTTP-AAC or HTTP-MP3 player.
class SrsHttpConsumer : public SrsMediaConsumer
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    // A raw pointer, since the source holds this consumer.
    ISrsHttpSource *source_;

public:
    SrsHttpConsumer(ISrsHttpSource *s);
    virtual ~SrsHttpConsumer();
};

// The source of the HTTP stream players, fed by the RTMP source of the stream, whoever publishes it. It mounts the
// HTTP stream while it publishes, and keeps the metadata, sequence headers and GOP for a new player.
class SrsHttpSource : public ISrsHttpSource
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppConfig *config_;
    ISrsHttpSourceHandler *handler_;
    ISrsClock *clk_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsRequest *req_;
    std::vector<SrsHttpConsumer *> consumers_;
    SrsRtmpJitterAlgorithm jitter_algorithm_;
    bool atc_;
    bool can_publish_;
    srs_utime_t stream_die_at_;
    SrsGopCache *gop_cache_;
    SrsMetaCache *meta_;

public:
    SrsHttpSource();
    void assemble(); // Not in the constructor, to avoid calling functions there.
    virtual ~SrsHttpSource();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual bool stream_is_dead();
    virtual void update_auth(ISrsRequest *r);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    // The RTMP source parses the stream, so it tells which message is a sequence header and which repeats the last
    // one, for the HTTP players to get what the RTMP players get.
    virtual srs_error_t on_meta_data(SrsMediaPacket *shared_metadata, bool drop_for_reduce);
    virtual srs_error_t on_audio(SrsMediaPacket *shared_audio, bool is_sequence_header, bool drop_for_reduce);
    virtual srs_error_t on_video(SrsMediaPacket *shared_video, bool is_sequence_header, bool drop_for_reduce);

public:
    virtual srs_error_t create_consumer(SrsHttpConsumer *&consumer);
    // ds dumps the sequence headers, dm the metadata, and dg the GOP cache.
    virtual srs_error_t consumer_dumps(ISrsMediaConsumer *consumer, bool ds = true, bool dm = true, bool dg = true);
    virtual void on_consumer_destroy(SrsHttpConsumer *consumer);
    virtual void set_cache(bool enabled);
    virtual void set_gop_cache_max_frames(int v);
    virtual SrsRtmpJitterAlgorithm jitter();
};

class ISrsHttpSourceManager
{
public:
    ISrsHttpSourceManager();
    virtual ~ISrsHttpSourceManager();

public:
    virtual srs_error_t initialize() = 0;
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsHttpSource> &pps) = 0;
    virtual SrsSharedPtr<SrsHttpSource> fetch(ISrsRequest *r) = 0;
};

class SrsHttpSourceManager : public ISrsHourGlassHandler, public ISrsHttpSourceManager
{
// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    ISrsAppFactory *app_factory_;

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    srs_mutex_t lock_;
    std::map<std::string, SrsSharedPtr<SrsHttpSource> > pool_;
    ISrsHourGlass *timer_;

public:
    SrsHttpSourceManager();
    void assemble(); // Creates the members, to avoid calling functions in the constructor.
    virtual ~SrsHttpSourceManager();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsHttpSource> &pps);
    virtual SrsSharedPtr<SrsHttpSource> fetch(ISrsRequest *r);

// clang-format off
SRS_DECLARE_PRIVATE: // clang-format on
    virtual srs_error_t setup_ticks();
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick);
};

extern SrsHttpSourceManager *_srs_http_sources;

#endif
