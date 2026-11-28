//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_http_source.hpp>

#include <algorithm>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_server.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>

#define SRS_HTTP_SOURCE_CLEANUP (3 * SRS_UTIME_SECONDS)

ISrsHttpSourceHandler::ISrsHttpSourceHandler()
{
}

ISrsHttpSourceHandler::~ISrsHttpSourceHandler()
{
}

ISrsHttpSource::ISrsHttpSource()
{
}

ISrsHttpSource::~ISrsHttpSource()
{
}

SrsHttpConsumer::SrsHttpConsumer(ISrsHttpSource *s)
{
    source_ = s;
}

SrsHttpConsumer::~SrsHttpConsumer()
{
    source_->on_consumer_destroy(this);
}

SrsHttpSource::SrsHttpSource()
{
    req_ = NULL;
    jitter_algorithm_ = SrsRtmpJitterAlgorithmOFF;
    atc_ = false;
    can_publish_ = true;
    stream_die_at_ = 0;
    gop_cache_ = new SrsGopCache();
    meta_ = new SrsMetaCache();

    config_ = _srs_config;
    handler_ = _srs_server;
    clk_ = _srs_clock;
}

void SrsHttpSource::assemble()
{
    // Otherwise stream_is_dead() takes a new source as dead at once.
    stream_die_at_ = clk_->now();
}

SrsHttpSource::~SrsHttpSource()
{
    // Never free the consumers; they free themselves.
    consumers_.clear();

    srs_freep(gop_cache_);
    srs_freep(meta_);
    srs_freep(req_);

    config_ = NULL;
    handler_ = NULL;
    clk_ = NULL;
}

// Called after fetch_or_create puts the source in the pool, so it must not switch coroutines.
srs_error_t SrsHttpSource::initialize(ISrsRequest *r)
{
    req_ = r->copy();
    atc_ = config_->get_atc(req_->vhost_);
    jitter_algorithm_ = (SrsRtmpJitterAlgorithm)config_->get_time_jitter(req_->vhost_);

    return srs_success;
}

bool SrsHttpSource::stream_is_dead()
{
    if (!can_publish_ || !consumers_.empty()) {
        return false;
    }

    return clk_->now() >= stream_die_at_ + SRS_HTTP_SOURCE_CLEANUP;
}

void SrsHttpSource::update_auth(ISrsRequest *r)
{
    req_->update_auth(r);
}

srs_error_t SrsHttpSource::on_publish()
{
    srs_error_t err = srs_success;

    can_publish_ = false;

    // Reset the metadata cache, for VLC when the stream is disabled and enabled.
    // @see https://github.com/ossrs/srs/issues/1630#issuecomment-597979448
    meta_->clear();

    if ((err = handler_->http_mount(req_)) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }

    return err;
}

void SrsHttpSource::on_unpublish()
{
    if (can_publish_) {
        return;
    }

    gop_cache_->clear();

    handler_->http_unmount(req_);

    if (consumers_.empty()) {
        stream_die_at_ = clk_->now();
    }

    // Set it only after the HTTP stream is unmounted, which may switch coroutines.
    can_publish_ = true;
}

srs_error_t SrsHttpSource::on_meta_data(SrsMediaPacket *shared_metadata, bool drop_for_reduce)
{
    srs_error_t err = srs_success;

    atc_ = config_->get_atc(req_->vhost_);
    meta_->update_data(shared_metadata);

    if (drop_for_reduce) {
        return err;
    }

    for (int i = 0; i < (int)consumers_.size(); i++) {
        SrsHttpConsumer *consumer = consumers_.at(i);
        if ((err = consumer->enqueue(meta_->data(), atc_, jitter_algorithm_)) != srs_success) {
            return srs_error_wrap(err, "consume metadata");
        }
    }

    return err;
}

srs_error_t SrsHttpSource::on_audio(SrsMediaPacket *shared_audio, bool is_sequence_header, bool drop_for_reduce)
{
    srs_error_t err = srs_success;

    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsHttpConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(shared_audio, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume audio");
            }
        }
    }

    if (is_sequence_header || !meta_->ash()) {
        if ((err = meta_->update_ash(shared_audio)) != srs_success) {
            return srs_error_wrap(err, "meta consume audio");
        }
    }

    if (is_sequence_header) {
        return err;
    }

    if ((err = gop_cache_->cache(shared_audio)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume audio");
    }

    if (atc_) {
        if (meta_->ash()) {
            meta_->ash()->timestamp_ = shared_audio->timestamp_;
        }
        if (meta_->data()) {
            meta_->data()->timestamp_ = shared_audio->timestamp_;
        }
    }

    return err;
}

srs_error_t SrsHttpSource::on_video(SrsMediaPacket *shared_video, bool is_sequence_header, bool drop_for_reduce)
{
    srs_error_t err = srs_success;

    if (is_sequence_header) {
        meta_->vsh_format()->avc_parse_sps_ = config_->get_parse_sps(req_->vhost_);
        if ((err = meta_->update_vsh(shared_video)) != srs_success) {
            return srs_error_wrap(err, "meta update video");
        }
    }

    if (!drop_for_reduce) {
        for (int i = 0; i < (int)consumers_.size(); i++) {
            SrsHttpConsumer *consumer = consumers_.at(i);
            if ((err = consumer->enqueue(shared_video, atc_, jitter_algorithm_)) != srs_success) {
                return srs_error_wrap(err, "consume video");
            }
        }
    }

    if (is_sequence_header) {
        return err;
    }

    if ((err = gop_cache_->cache(shared_video)) != srs_success) {
        return srs_error_wrap(err, "gop cache consume video");
    }

    if (atc_) {
        if (meta_->vsh()) {
            meta_->vsh()->timestamp_ = shared_video->timestamp_;
        }
        if (meta_->data()) {
            meta_->data()->timestamp_ = shared_video->timestamp_;
        }
    }

    return err;
}

srs_error_t SrsHttpSource::create_consumer(SrsHttpConsumer *&consumer)
{
    consumer = new SrsHttpConsumer(this);
    consumers_.push_back(consumer);

    stream_die_at_ = 0;

    return srs_success;
}

srs_error_t SrsHttpSource::consumer_dumps(ISrsMediaConsumer *consumer, bool ds, bool dm, bool dg)
{
    srs_error_t err = srs_success;

    srs_utime_t queue_size = config_->get_queue_length(req_->vhost_);
    consumer->set_queue_size(queue_size);

    if (atc_ && !gop_cache_->empty()) {
        if (meta_->data()) {
            meta_->data()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
        if (meta_->vsh()) {
            meta_->vsh()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
        if (meta_->ash()) {
            meta_->ash()->timestamp_ = srsu2ms(gop_cache_->start_time());
        }
    }

    bool active = !can_publish_;
    if (active) {
        if ((err = meta_->dumps(consumer, atc_, jitter_algorithm_, dm, ds)) != srs_success) {
            return srs_error_wrap(err, "meta dumps");
        }

        if (dg && (err = gop_cache_->dump(consumer, atc_, jitter_algorithm_)) != srs_success) {
            return srs_error_wrap(err, "gop cache dumps");
        }
    }

    if (dg) {
        srs_trace("create http consumer, active=%d, queue_size=%dms, jitter=%d", active, srsu2msi(queue_size), jitter_algorithm_);
    } else {
        srs_trace("create http consumer, active=%d, ignore gop cache, jitter=%d", active, jitter_algorithm_);
    }

    return err;
}

void SrsHttpSource::on_consumer_destroy(SrsHttpConsumer *consumer)
{
    std::vector<SrsHttpConsumer *>::iterator it = std::find(consumers_.begin(), consumers_.end(), consumer);
    if (it != consumers_.end()) {
        consumers_.erase(it);
    }

    if (consumers_.empty() && can_publish_) {
        stream_die_at_ = clk_->now();
    }
}

void SrsHttpSource::set_cache(bool enabled)
{
    gop_cache_->set(enabled);
}

void SrsHttpSource::set_gop_cache_max_frames(int v)
{
    gop_cache_->set_gop_cache_max_frames(v);
}

SrsRtmpJitterAlgorithm SrsHttpSource::jitter()
{
    return jitter_algorithm_;
}

ISrsHttpSourceManager::ISrsHttpSourceManager()
{
}

ISrsHttpSourceManager::~ISrsHttpSourceManager()
{
}

SrsHttpSourceManager::SrsHttpSourceManager()
{
    lock_ = srs_mutex_new();
    timer_ = NULL;

    app_factory_ = _srs_app_factory;
}

void SrsHttpSourceManager::assemble()
{
    timer_ = app_factory_->create_hourglass("http-sources", this, 1 * SRS_UTIME_SECONDS);
}

SrsHttpSourceManager::~SrsHttpSourceManager()
{
    srs_mutex_destroy(lock_);
    srs_freep(timer_);

    app_factory_ = NULL;
}

srs_error_t SrsHttpSourceManager::initialize()
{
    return setup_ticks();
}

srs_error_t SrsHttpSourceManager::setup_ticks()
{
    srs_error_t err = srs_success;

    if ((err = timer_->tick(1, 3 * SRS_UTIME_SECONDS)) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsHttpSourceManager::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    std::map<std::string, SrsSharedPtr<SrsHttpSource> >::iterator it;
    for (it = pool_.begin(); it != pool_.end();) {
        SrsSharedPtr<SrsHttpSource> &source = it->second;

        if (source->stream_is_dead()) {
            srs_trace("HTTP: cleanup die source, url=%s, total=%d", it->first.c_str(), (int)pool_.size());
            pool_.erase(it++);
        } else {
            ++it;
        }
    }

    return srs_success;
}

srs_error_t SrsHttpSourceManager::fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsHttpSource> &pps)
{
    srs_error_t err = srs_success;

    bool created = false;
    if (true) {
        // The lock guards against a coroutine switch.
        // @bug https://github.com/ossrs/srs/issues/1230
        SrsLocker(&lock_);

        string stream_url = r->get_stream_url();
        std::map<std::string, SrsSharedPtr<SrsHttpSource> >::iterator it = pool_.find(stream_url);
        if (it != pool_.end()) {
            pps = it->second;
        } else {
            SrsSharedPtr<SrsHttpSource> source(new SrsHttpSource());
            source->assemble();
            srs_trace("new http source, stream_url=%s", stream_url.c_str());
            pps = source;

            pool_[stream_url] = source;
            created = true;
        }
    }

    if (created && (err = pps->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "init source %s", r->get_stream_url().c_str());
    }

    // With origin auth on, the token of the old request may be invalid.
    if (!created) {
        pps->update_auth(r);
    }

    return err;
}

SrsSharedPtr<SrsHttpSource> SrsHttpSourceManager::fetch(ISrsRequest *r)
{
    // The lock guards against a coroutine switch.
    // @bug https://github.com/ossrs/srs/issues/1230
    SrsLocker(&lock_);

    SrsSharedPtr<SrsHttpSource> source;

    std::map<std::string, SrsSharedPtr<SrsHttpSource> >::iterator it = pool_.find(r->get_stream_url());
    if (it != pool_.end()) {
        source = it->second;
    }

    return source;
}

SrsHttpSourceManager *_srs_http_sources = NULL;
