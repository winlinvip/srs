//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_STREAM_BRIDGE_HPP
#define SRS_APP_STREAM_BRIDGE_HPP

#include <srs_core.hpp>

#include <srs_core_autofree.hpp>
#include <srs_kernel_codec.hpp>

#include <vector>

class ISrsRequest;
class SrsMediaPacket;
class SrsLiveSource;
class SrsRtcSource;
class SrsRtmpFormat;
class SrsMetaCache;
class SrsRtpPacket;
class SrsRtcRtpBuilder;
class SrsRtspSource;
class SrsRtspRtpBuilder;
class SrsRtcFrameBuilder;
class ISrsStreamBridge;
class SrsSrtFrameBuilder;
class SrsSrtPacket;

// A target to feed AV frame, such as a RTMP live source.
class ISrsFrameTarget
{
public:
    ISrsFrameTarget();
    virtual ~ISrsFrameTarget();

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame) = 0;
};

// A target to feed RTP packets, such as a RTC source.
class ISrsRtpTarget
{
public:
    ISrsRtpTarget();
    virtual ~ISrsRtpTarget();

public:
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt) = 0;
};

// A target to feed SRT packets, such as a SRT source.
class ISrsSrtTarget
{
public:
    ISrsSrtTarget();
    virtual ~ISrsSrtTarget();

public:
    virtual srs_error_t on_packet(SrsSrtPacket *pkt) = 0;
};

// A RTC RTP bridge is used to convert RTP packets to different protocols,
// such as bridge to RTMP.
class ISrsRtcBridge : public ISrsRtpTarget
{
public:
    ISrsRtcBridge();
    virtual ~ISrsRtcBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

// A RTMP bridge is used to convert RTMP stream to different protocols,
// such as bridge to RTC and RTSP.
class ISrsRtmpBridge : public ISrsFrameTarget
{
public:
    ISrsRtmpBridge();
    virtual ~ISrsRtmpBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

// A RTMP bridge to convert RTMP stream to different protocols, such as RTC and RTSP.
// First, it use a RTP builder to convert RTMP frame to RTP packets.
// Then, deliver the RTP packets to RTP target, which binds to a RTC/RTSP source.
class SrsRtmpBridge : public ISrsRtmpBridge
{
private:
#ifdef SRS_FFMPEG_FIT
    SrsRtcRtpBuilder *rtp_builder_;
#endif
#ifdef SRS_RTSP
    SrsRtspRtpBuilder *rtsp_builder_;
#endif
    // The Source bridge, bridge stream to other source.
    SrsSharedPtr<SrsRtcSource> rtc_target_;
    SrsSharedPtr<SrsRtspSource> rtsp_target_;

public:
    SrsRtmpBridge();
    virtual ~SrsRtmpBridge();

public:
    bool empty();
    void enable_rtmp2rtc(SrsSharedPtr<SrsRtcSource> rtc_source);
    void enable_rtmp2rtsp(SrsSharedPtr<SrsRtspSource> rtsp_source);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
};

// A SRT bridge is used to convert SRT stream to different protocols,
// such as bridge to RTMP and RTC.
class ISrsSrtBridge : public ISrsSrtTarget
{
public:
    ISrsSrtBridge();
    virtual ~ISrsSrtBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual void on_unpublish() = 0;
};

// A SRT bridge to convert SRT stream to different protocols, such as RTMP and RTC.
// First, it use a frame builder to convert SRT TS packets to AV frames.
// Then, deliver the AV frames to frame target, which binds to a RTMP/RTC source.
class SrsSrtBridge : public ISrsSrtBridge, public ISrsFrameTarget
{
private:
    // Convert SRT TS packets to media frame packets.
    SrsSrtFrameBuilder *frame_builder_;
    // Deliver media frame packets to RTMP target.
    SrsSharedPtr<SrsLiveSource> rtmp_target_;
    // Convert media frame packets to RTP packets.
#ifdef SRS_FFMPEG_FIT
    SrsRtcRtpBuilder *rtp_builder_;
#endif
    // Deliver RTP packets to RTC target.
    SrsSharedPtr<SrsRtcSource> rtc_target_;

public:
    SrsSrtBridge();
    virtual ~SrsSrtBridge();

public:
    bool empty();
    void enable_srt2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_source);
    void enable_srt2rtc(SrsSharedPtr<SrsRtcSource> rtc_source);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_packet(SrsSrtPacket *pkt);
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
};

// A RTC bridge to convert RTP packets to RTMP stream.
// First, it use a frame builder to convert RTP packets to RTMP frame packet.
// Then, deliver the RTMP frame packet to RTMP target, which binds to a live source.
class SrsRtcBridge : public ISrsRtcBridge
{
private:
    ISrsRequest *req_;
#ifdef SRS_FFMPEG_FIT
    // Collect and build WebRTC RTP packets to AV frames.
    SrsRtcFrameBuilder *frame_builder_;
#endif
    // The Source bridge, bridge stream to other source.
    SrsSharedPtr<SrsLiveSource> rtmp_target_;

public:
    SrsRtcBridge();
    virtual ~SrsRtcBridge();

public:
    bool empty();
    void enable_rtc2rtmp(SrsSharedPtr<SrsLiveSource> rtmp_target);

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t setup_codec(SrsAudioCodecId acodec, SrsVideoCodecId vcodec);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_rtp(SrsRtpPacket *pkt);
};

// A stream bridge is used to convert stream via different protocols, such as bridge for RTMP and RTC. Generally, we use
// frame as message for bridge. A frame is a audio or video frame, such as an I/B/P frame, a general frame for decoder.
// So you must assemble RTP or TS packets to a video frame if WebRTC or SRT.
class ISrsStreamBridge
{
public:
    ISrsStreamBridge();
    virtual ~ISrsStreamBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r) = 0;
    virtual srs_error_t on_publish() = 0;
    virtual srs_error_t on_frame(SrsMediaPacket *frame) = 0;
    virtual void on_unpublish() = 0;
};

// A bridge to feed AV frame to RTMP stream.
class SrsFrameToRtmpBridge : public ISrsStreamBridge
{
private:
    SrsSharedPtr<SrsLiveSource> source_;

public:
    SrsFrameToRtmpBridge(SrsSharedPtr<SrsLiveSource> source);
    virtual ~SrsFrameToRtmpBridge();

public:
    srs_error_t initialize(ISrsRequest *r);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
};

// A bridge to covert AV frame to WebRTC stream.
class SrsFrameToRtcBridge : public ISrsStreamBridge, public ISrsRtpTarget
{
private:
    SrsSharedPtr<SrsRtcSource> source_;

private:
#if defined(SRS_FFMPEG_FIT)
    SrsRtcRtpBuilder *rtp_builder_;
#endif
public:
    SrsFrameToRtcBridge(SrsSharedPtr<SrsRtcSource> source);
    virtual ~SrsFrameToRtcBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    srs_error_t on_rtp(SrsRtpPacket *pkt);
};

#ifdef SRS_RTSP
// A bridge to covert AV frame to RTSP stream.
class SrsFrameToRtspBridge : public ISrsStreamBridge, public ISrsRtpTarget
{
private:
    SrsSharedPtr<SrsRtspSource> source_;

private:
    SrsRtspRtpBuilder *rtp_builder_;

public:
    SrsFrameToRtspBridge(SrsSharedPtr<SrsRtspSource> source);
    virtual ~SrsFrameToRtspBridge();

public:
    virtual srs_error_t initialize(ISrsRequest *r);
    virtual srs_error_t on_publish();
    virtual void on_unpublish();
    virtual srs_error_t on_frame(SrsMediaPacket *frame);
    srs_error_t on_rtp(SrsRtpPacket *pkt);
};
#endif

// A bridge chain, a set of bridges.
class SrsCompositeBridge : public ISrsStreamBridge
{
public:
    SrsCompositeBridge();
    virtual ~SrsCompositeBridge();

public:
    bool empty() { return bridges_.empty(); } // SrsCompositeBridge::empty()
public:
    srs_error_t initialize(ISrsRequest *r);

public:
    virtual srs_error_t on_publish();
    virtual void on_unpublish();

public:
    virtual srs_error_t on_frame(SrsMediaPacket *frame);

public:
    SrsCompositeBridge *append(ISrsStreamBridge *bridge);

private:
    std::vector<ISrsStreamBridge *> bridges_;
};

#endif
