//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_stream_bridge.hpp>

#include <srs_app_config.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_app_rtc_source.hpp>

#include <vector>
using namespace std;

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

ISrsRtcBridge::ISrsRtcBridge()
{
}

ISrsRtcBridge::~ISrsRtcBridge()
{
}

SrsRtcBridge::SrsRtcBridge()
{
    req_ = NULL;
#ifdef SRS_FFMPEG_FIT
    frame_builder_ = NULL;
#endif
    rtmp_target_ = NULL;
}

SrsRtcBridge::~SrsRtcBridge()
{
    srs_freep(req_);
#ifdef SRS_FFMPEG_FIT
    srs_freep(frame_builder_);
#endif
    rtmp_target_ = NULL;
}

void SrsRtcBridge::enable_rtc2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_target)
{
    rtmp_target_ = rtmp_target;
}

srs_error_t SrsRtcBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    srs_freep(req_);
    req_ = r->copy();

#ifdef SRS_FFMPEG_FIT
    srs_assert(rtmp_target_.get());
    srs_freep(frame_builder_);
    frame_builder_ = new SrsRtcFrameBuilder(rtmp_target_.get());
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
    if ((err = rtmp_target_->on_publish()) != srs_success) {
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

    // Note that RTC source free this rtc bridge, after on_unpublish() is called.
    // So there is no need to free its components here.
}

srs_error_t SrsRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    srs_error_t err = srs_success;

#ifdef SRS_FFMPEG_FIT
    srs_assert(frame_builder_);
    if ((err = frame_builder_->on_rtp(pkt)) != srs_success) {
        return srs_error_wrap(err, "frame builder on rtp");
    }
#endif

    return err;
}

ISrsStreamBridge::ISrsStreamBridge()
{
}

ISrsStreamBridge::~ISrsStreamBridge()
{
}

SrsFrameToRtmpBridge::SrsFrameToRtmpBridge(SrsSharedPtr<SrsLiveSource> source)
{
    source_ = source;
}

SrsFrameToRtmpBridge::~SrsFrameToRtmpBridge()
{
}

srs_error_t SrsFrameToRtmpBridge::initialize(ISrsRequest *r)
{
    return srs_success;
}

srs_error_t SrsFrameToRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    return err;
}

void SrsFrameToRtmpBridge::on_unpublish()
{
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsFrameToRtmpBridge::on_frame(SrsMediaPacket *frame)
{
    return source_->on_frame(frame);
}

SrsFrameToRtcBridge::SrsFrameToRtcBridge(SrsSharedPtr<SrsRtcSource> source)
{
    source_ = source;

#if defined(SRS_FFMPEG_FIT)
    // Use lazy initialization - no need to determine codec/track parameters here
    rtp_builder_ = new SrsRtcRtpBuilder(this, source);
#endif
}

SrsFrameToRtcBridge::~SrsFrameToRtcBridge()
{
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif
}

srs_error_t SrsFrameToRtcBridge::initialize(ISrsRequest *r)
{
#ifdef SRS_FFMPEG_FIT
    return rtp_builder_->initialize(r);
#else
    return srs_success;
#endif
}

srs_error_t SrsFrameToRtcBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

#ifdef SRS_FFMPEG_FIT
    if ((err = rtp_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtp builder publish");
    }
#endif

    return err;
}

void SrsFrameToRtcBridge::on_unpublish()
{
#ifdef SRS_FFMPEG_FIT
    rtp_builder_->on_unpublish();
#endif

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsFrameToRtcBridge::on_frame(SrsMediaPacket *frame)
{
#ifdef SRS_FFMPEG_FIT
    return rtp_builder_->on_frame(frame);
#else
    return srs_success;
#endif
}

srs_error_t SrsFrameToRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    return source_->on_rtp(pkt);
}

#ifdef SRS_RTSP
SrsFrameToRtspBridge::SrsFrameToRtspBridge(SrsSharedPtr<SrsRtspSource> source)
{
    source_ = source;

    // Use lazy initialization - no need to determine codec/track parameters here
    rtp_builder_ = new SrsRtspRtpBuilder(this, source);
}

SrsFrameToRtspBridge::~SrsFrameToRtspBridge()
{
    srs_freep(rtp_builder_);
}

srs_error_t SrsFrameToRtspBridge::initialize(ISrsRequest *r)
{
    return rtp_builder_->initialize(r);
}

srs_error_t SrsFrameToRtspBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    if ((err = rtp_builder_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "rtp builder publish");
    }

    return err;
}

void SrsFrameToRtspBridge::on_unpublish()
{
    rtp_builder_->on_unpublish();

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsFrameToRtspBridge::on_frame(SrsMediaPacket *frame)
{
    return rtp_builder_->on_frame(frame);
}

srs_error_t SrsFrameToRtspBridge::on_rtp(SrsRtpPacket *pkt)
{
    return source_->on_rtp(pkt);
}
#endif

SrsCompositeBridge::SrsCompositeBridge()
{
}

SrsCompositeBridge::~SrsCompositeBridge()
{
    for (vector<ISrsStreamBridge *>::iterator it = bridges_.begin(); it != bridges_.end(); ++it) {
        ISrsStreamBridge *bridge = *it;
        srs_freep(bridge);
    }
}

srs_error_t SrsCompositeBridge::initialize(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    for (vector<ISrsStreamBridge *>::iterator it = bridges_.begin(); it != bridges_.end(); ++it) {
        ISrsStreamBridge *bridge = *it;
        if ((err = bridge->initialize(r)) != srs_success) {
            return err;
        }
    }

    return err;
}

srs_error_t SrsCompositeBridge::on_publish()
{
    srs_error_t err = srs_success;

    for (vector<ISrsStreamBridge *>::iterator it = bridges_.begin(); it != bridges_.end(); ++it) {
        ISrsStreamBridge *bridge = *it;
        if ((err = bridge->on_publish()) != srs_success) {
            return err;
        }
    }

    return err;
}

void SrsCompositeBridge::on_unpublish()
{
    for (vector<ISrsStreamBridge *>::iterator it = bridges_.begin(); it != bridges_.end(); ++it) {
        ISrsStreamBridge *bridge = *it;
        bridge->on_unpublish();
    }
}

srs_error_t SrsCompositeBridge::on_frame(SrsMediaPacket *frame)
{
    srs_error_t err = srs_success;

    for (vector<ISrsStreamBridge *>::iterator it = bridges_.begin(); it != bridges_.end(); ++it) {
        ISrsStreamBridge *bridge = *it;
        if ((err = bridge->on_frame(frame)) != srs_success) {
            return err;
        }
    }

    return err;
}

SrsCompositeBridge *SrsCompositeBridge::append(ISrsStreamBridge *bridge)
{
    bridges_.push_back(bridge);
    return this;
}
