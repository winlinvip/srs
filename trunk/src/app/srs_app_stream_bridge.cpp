//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_stream_bridge.hpp>

#include <srs_app_config.hpp>
#include <srs_app_factory.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_rtc_source.hpp>

using namespace std;

SrsBridgeRow::SrsBridgeRow()
{
    claim_rtmp_ = false;
    claim_rtc_ = false;
    claim_srt_ = false;
    rtmp_to_rtc_ = false;
    rtmp_to_rtsp_ = false;
    srt_to_rtmp_ = false;
    srt_to_rtc_ = false;
    rtc_to_rtmp_ = false;
    rtmp_source_ = NULL;
    rtc_source_ = NULL;
    srt_source_ = NULL;
}

SrsBridgeRow::~SrsBridgeRow()
{
    rtmp_source_ = NULL;
    rtc_source_ = NULL;
    srt_source_ = NULL;
}

void SrsBridgeRow::initialize(ISrsAppConfig *config, SrsStreamProtocol publisher, std::string vhost)
{
    bool edge = config->get_vhost_is_edge(vhost);
    bool rtc = config->get_rtc_server_enabled() && config->get_rtc_enabled(vhost);
    bool srt = config->get_srt_enabled() && config->get_srt_enabled(vhost);

    bool rtmp_publisher = publisher == SrsStreamProtocolRtmp;
    bool srt_publisher = publisher == SrsStreamProtocolSrt;
    bool rtc_publisher = publisher == SrsStreamProtocolRtc;

    claim_rtmp_ = !rtmp_publisher;
    claim_rtc_ = !rtc_publisher && rtc && !edge;
    claim_srt_ = !srt_publisher && srt && (rtc_publisher || !edge);

    rtmp_to_rtc_ = rtmp_publisher && rtc && config->get_rtc_from_rtmp(vhost);
#ifdef SRS_RTSP
    rtmp_to_rtsp_ = rtmp_publisher && config->get_rtsp_server_enabled() && config->get_rtsp_enabled(vhost) && config->get_rtsp_from_rtmp(vhost);
#endif
    srt_to_rtmp_ = srt_publisher && config->get_srt_to_rtmp(vhost);
    srt_to_rtc_ = srt_publisher && rtc && config->get_rtc_from_rtmp(vhost);
    rtc_to_rtmp_ = rtc_publisher && config->get_rtc_to_rtmp(vhost);

    if (edge && (rtmp_to_rtc_ || rtmp_to_rtsp_ || srt_to_rtmp_ || srt_to_rtc_ || rtc_to_rtmp_)) {
        rtmp_to_rtc_ = rtmp_to_rtsp_ = srt_to_rtmp_ = srt_to_rtc_ = rtc_to_rtmp_ = false;
        srs_warn("disable bridges for edge vhost=%s", vhost.c_str());
    }
}

srs_error_t SrsBridgeRow::claim(ISrsRequest *r, ISrsRtmpSourceManager *rtmp_sources, ISrsRtcSourceManager *rtc_sources, ISrsSrtSourceManager *srt_sources)
{
    srs_error_t err = srs_success;

    if (claim_rtmp_ && (err = rtmp_sources->fetch_or_create(r, rtmp_source_)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (claim_rtc_ && (err = rtc_sources->fetch_or_create(r, rtc_source_)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (claim_srt_ && (err = srt_sources->fetch_or_create(r, srt_source_)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (rtmp_source_.get() && !rtmp_source_->can_publish(false)) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtmp stream %s busy", r->get_stream_url().c_str());
    }

    if (rtc_source_.get() && !rtc_source_->can_publish()) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "rtc stream %s busy", r->get_stream_url().c_str());
    }

    if (srt_source_.get() && !srt_source_->can_publish()) {
        return srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "srt stream %s busy", r->get_stream_url().c_str());
    }

    return err;
}

ISrsFrameTarget::ISrsFrameTarget()
{
}

ISrsFrameTarget::~ISrsFrameTarget()
{
}

ISrsRtpTarget::ISrsRtpTarget()
{
}

ISrsRtpTarget::~ISrsRtpTarget()
{
}

ISrsSrtTarget::ISrsSrtTarget()
{
}

ISrsSrtTarget::~ISrsSrtTarget()
{
}

ISrsRtmpBridge::ISrsRtmpBridge()
{
}

ISrsRtmpBridge::~ISrsRtmpBridge()
{
}

SrsRtmpBridge::SrsRtmpBridge(ISrsAppFactory *factory)
{
#ifdef SRS_FFMPEG_FIT
    rtp_builder_ = NULL;
#endif
#ifdef SRS_RTSP
    rtsp_builder_ = NULL;
    rtsp_target_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = factory;
}

SrsRtmpBridge::~SrsRtmpBridge()
{
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif
#ifdef SRS_RTSP
    srs_freep(rtsp_builder_);
    rtsp_target_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = NULL;
}

bool SrsRtmpBridge::empty()
{
    if (rtc_target_.get()) {
        return false;
    }

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        return false;
    }
#endif

    return true;
}

void SrsRtmpBridge::enable_rtmp2rtc(SrsSharedPtr<SrsRtcSource> rtc_source)
{
    rtc_target_ = rtc_source;
}

#ifdef SRS_RTSP
void SrsRtmpBridge::enable_rtmp2rtsp(SrsSharedPtr<SrsRtspSource> rtsp_source)
{
    rtsp_target_ = rtsp_source;
}
#endif

srs_error_t SrsRtmpBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (rtc_target_.get()) {
        srs_freep(rtp_builder_);
        rtp_builder_ = new SrsRtcRtpBuilder(app_factory_, rtc_target_.get(), rtc_target_);
        rtp_builder_->assemble();
        if ((err = rtp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtp builder initialize");
        }
    }
#endif

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        srs_freep(rtsp_builder_);
        rtsp_builder_ = new SrsRtspRtpBuilder(rtsp_target_.get(), rtsp_target_);
        if ((err = rtsp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtsp builder initialize");
        }
    }
#endif

    return err;
}

srs_error_t SrsRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if (rtc_target_.get()) {
        if ((err = rtc_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtc target publish");
        }

#ifdef SRS_FFMPEG_FIT
        if ((err = rtp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtp builder publish");
        }
#endif
    }

#ifdef SRS_RTSP
    // TODO: FIXME: Should sync with bridge?
    if (rtsp_target_.get()) {
        if ((err = rtsp_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtsp target publish");
        }

        if ((err = rtsp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtsp builder publish");
        }
    }
#endif

    return err;
}

void SrsRtmpBridge::on_unpublish()
{
    if (rtc_target_.get()) {
#ifdef SRS_FFMPEG_FIT
        rtp_builder_->on_unpublish();
#endif
        rtc_target_->on_unpublish();
    }

#ifdef SRS_RTSP
    if (rtsp_target_.get()) {
        rtsp_builder_->on_unpublish();
        rtsp_target_->on_unpublish();
    }
#endif

    // The RTMP source frees this bridge right after on_unpublish(), so the components are not freed here.
}

srs_error_t SrsRtmpBridge::on_frame(SrsMediaPacket *frame)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (rtp_builder_ && (err = rtp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtp builder on frame");
    }
#endif

#ifdef SRS_RTSP
    if (rtsp_builder_ && (err = rtsp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtsp builder on frame");
    }
#endif

    return err;
}

ISrsSrtBridge::ISrsSrtBridge()
{
}

ISrsSrtBridge::~ISrsSrtBridge()
{
}

SrsSrtBridge::SrsSrtBridge(ISrsAppFactory *factory)
{
    frame_builder_ = new SrsSrtFrameBuilder(this);
    rtmp_target_ = NULL;

#ifdef SRS_FFMPEG_FIT
    rtp_builder_ = NULL;
#endif
    rtc_target_ = NULL;

    app_factory_ = factory;
}

SrsSrtBridge::~SrsSrtBridge()
{
    rtmp_target_ = NULL;
    srs_freep(frame_builder_);

    rtc_target_ = NULL;
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif

    app_factory_ = NULL;
}

void SrsSrtBridge::assemble()
{
    frame_builder_->assemble();
}

bool SrsSrtBridge::empty()
{
    return !rtmp_target_.get() && !rtc_target_.get();
}

void SrsSrtBridge::enable_srt2rtmp(SrsSharedPtr<SrsRtmpSource> rtmp_source)
{
    rtmp_target_ = rtmp_source;
}

void SrsSrtBridge::enable_srt2rtc(SrsSharedPtr<SrsRtcSource> rtc_source)
{
    rtc_target_ = rtc_source;
}

srs_error_t SrsSrtBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->initialize(r)) != srs_success) {
        return srs_error_wrap(err, "frame builder initialize");
    }

#ifdef SRS_FFMPEG_FIT
    if (rtc_target_.get()) {
        srs_freep(rtp_builder_);
        rtp_builder_ = new SrsRtcRtpBuilder(app_factory_, rtc_target_.get(), rtc_target_);
        rtp_builder_->assemble();
        if ((err = rtp_builder_->initialize(r)) != srs_success) {
            return srs_error_wrap(err, "rtp builder initialize");
        }
    }
#endif

    return err;
}

srs_error_t SrsSrtBridge::on_publish()
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "frame builder publish");
    }

    // TODO: FIXME: Should sync with bridge?
    if (rtmp_target_.get()) {
        if ((err = rtmp_target_->on_publish_by(SrsStreamProtocolSrt)) != srs_success) {
            return srs_error_wrap(err, "rtmp target publish");
        }
    }

    if (rtc_target_.get()) {
        if ((err = rtc_target_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtc target publish");
        }

#ifdef SRS_FFMPEG_FIT
        if ((err = rtp_builder_->on_publish()) != srs_success) {
            return srs_error_wrap(err, "rtp builder publish");
        }
#endif
    }

    return err;
}

void SrsSrtBridge::on_unpublish()
{
    frame_builder_->on_unpublish();

    if (rtmp_target_.get()) {
        rtmp_target_->on_unpublish();
    }

    if (rtc_target_.get()) {
#ifdef SRS_FFMPEG_FIT
        rtp_builder_->on_unpublish();
#endif
        rtc_target_->on_unpublish();
    }

    // The SRT source frees this bridge right after on_unpublish(), so the components are not freed here.
}

srs_error_t SrsSrtBridge::on_srt_packet(SrsSrtPacket *pkt)
{
    srs_error_t err = srs_success;

    if ((err = frame_builder_->on_srt_packet(pkt)) != srs_success) {
        return srs_error_wrap(err, "frame builder on packet");
    }

    return err;
}

srs_error_t SrsSrtBridge::on_frame(SrsMediaPacket *frame)
{
    srs_error_t err = srs_success;

    if (rtmp_target_.get() && (err = rtmp_target_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtmp target on frame");
    }

#ifdef SRS_FFMPEG_FIT
    if (rtp_builder_ && (err = rtp_builder_->on_frame(frame)) != srs_success) {
        return srs_error_wrap(err, "rtp builder on frame");
    }
#endif

    return err;
}

ISrsRtcBridge::ISrsRtcBridge()
{
}

ISrsRtcBridge::~ISrsRtcBridge()
{
}

SrsRtcBridge::SrsRtcBridge(ISrsAppFactory *factory)
{
    req_ = NULL;
#ifdef SRS_FFMPEG_FIT
    frame_builder_ = NULL;
#endif
    rtmp_target_ = NULL;

    app_factory_ = factory;
}

SrsRtcBridge::~SrsRtcBridge()
{
    srs_freep(req_);
#ifdef SRS_FFMPEG_FIT
    srs_freep(frame_builder_);
#endif
    rtmp_target_ = NULL;

    app_factory_ = NULL;
}

void SrsRtcBridge::enable_rtc2rtmp(SrsSharedPtr<SrsRtmpSource> rtmp_target)
{
    rtmp_target_ = rtmp_target;
}

bool SrsRtcBridge::empty()
{
    return !rtmp_target_.get();
}

srs_error_t SrsRtcBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

#ifdef SRS_FFMPEG_FIT
    srs_assert(rtmp_target_.get());
    srs_freep(frame_builder_);
    frame_builder_ = app_factory_->create_rtc_frame_builder(rtmp_target_.get());
#endif

    return err;
}

srs_error_t SrsRtcBridge::setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    if ((err = frame_builder_->initialize(req_, acodec, vcodec)) != srs_success) {
        return srs_error_wrap(err, "frame builder initialize");
    }
#endif

    return err;
}

srs_error_t SrsRtcBridge::on_publish()
{
    srs_error_t err = srs_success;

    srs_assert(rtmp_target_.get());
    if ((err = rtmp_target_->on_publish_by(SrsStreamProtocolRtc)) != srs_success) {
        return srs_error_wrap(err, "rtmp target publish");
    }

#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    if ((err = frame_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "frame builder on publish");
    }
#endif

    return err;
}

void SrsRtcBridge::on_unpublish()
{
#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    frame_builder_->on_unpublish();
#endif

    srs_assert(rtmp_target_.get());
    rtmp_target_->on_unpublish();

    // The RTC source frees this bridge right after on_unpublish(), so the components are not freed here.
}

srs_error_t SrsRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    if (frame_builder_ && (err = frame_builder_->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "frame builder on rtp");
    }
#endif

    return err;
}
