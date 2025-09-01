//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_stream_bridge.hpp>

#include <srs_app_config.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_source.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_rtc_rtp.hpp>
#include <srs_protocol_format.hpp>
#include <srs_protocol_rtmp_stack.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif

#include <vector>
using namespace std;

ISrsStreamBridge::ISrsStreamBridge()
{
}

ISrsStreamBridge::~ISrsStreamBridge()
{
}

SrsParsedPacketToRtmpBridge::SrsParsedPacketToRtmpBridge(SrsSharedPtr<SrsLiveSource> source)
{
    source_ = source;
}

SrsParsedPacketToRtmpBridge::~SrsParsedPacketToRtmpBridge()
{
}

srs_error_t SrsParsedPacketToRtmpBridge::initialize(ISrsRequest *r)
{
    return srs_success;
}

srs_error_t SrsParsedPacketToRtmpBridge::on_publish()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Should sync with bridge?
    if ((err = source_->on_publish()) != srs_success) {
        return srs_error_wrap(err, "source publish");
    }

    return err;
}

void SrsParsedPacketToRtmpBridge::on_unpublish()
{
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsParsedPacketToRtmpBridge::on_frame(SrsMediaPacket *frame)
{
    return source_->on_frame(frame);
}

SrsParsedPacketToRtcBridge::SrsParsedPacketToRtcBridge(SrsSharedPtr<SrsRtcSource> source)
{
    source_ = source;

#if defined(SRS_FFMPEG_FIT)
    // Use lazy initialization - no need to determine codec/track parameters here
    rtp_builder_ = new SrsRtcRtpBuilder(this, source);
#endif
}

SrsParsedPacketToRtcBridge::~SrsParsedPacketToRtcBridge()
{
#ifdef SRS_FFMPEG_FIT
    srs_freep(rtp_builder_);
#endif
}

srs_error_t SrsParsedPacketToRtcBridge::initialize(ISrsRequest *r)
{
#ifdef SRS_FFMPEG_FIT
    return rtp_builder_->initialize(r);
#else
    return srs_success;
#endif
}

srs_error_t SrsParsedPacketToRtcBridge::on_publish()
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

void SrsParsedPacketToRtcBridge::on_unpublish()
{
#ifdef SRS_FFMPEG_FIT
    rtp_builder_->on_unpublish();
#endif

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsParsedPacketToRtcBridge::on_frame(SrsMediaPacket *frame)
{
#ifdef SRS_FFMPEG_FIT
    return rtp_builder_->on_frame(frame);
#else
    return srs_success;
#endif
}

srs_error_t SrsParsedPacketToRtcBridge::on_rtp(SrsRtpPacket *pkt)
{
    return source_->on_rtp(pkt);
}

#ifdef SRS_RTSP
SrsParsedPacketToRtspBridge::SrsParsedPacketToRtspBridge(SrsSharedPtr<SrsRtspSource> source)
{
    source_ = source;

    // Use lazy initialization - no need to determine codec/track parameters here
    rtp_builder_ = new SrsRtspRtpBuilder(this, source);
}

SrsParsedPacketToRtspBridge::~SrsParsedPacketToRtspBridge()
{
    srs_freep(rtp_builder_);
}

srs_error_t SrsParsedPacketToRtspBridge::initialize(ISrsRequest *r)
{
    return rtp_builder_->initialize(r);
}

srs_error_t SrsParsedPacketToRtspBridge::on_publish()
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

void SrsParsedPacketToRtspBridge::on_unpublish()
{
    rtp_builder_->on_unpublish();

    // @remark This bridge might be disposed here, so never use it.
    // TODO: FIXME: Should sync with bridge?
    source_->on_unpublish();
}

srs_error_t SrsParsedPacketToRtspBridge::on_frame(SrsMediaPacket *frame)
{
    return rtp_builder_->on_frame(frame);
}

srs_error_t SrsParsedPacketToRtspBridge::on_rtp(SrsRtpPacket *pkt)
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
