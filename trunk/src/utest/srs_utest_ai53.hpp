//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI53_HPP
#define SRS_UTEST_AI53_HPP

/*
#include <srs_utest_ai53.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_source.hpp>
#include <srs_app_srt_source.hpp>
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_utest_ai08.hpp>
#include <srs_utest_manual_mock.hpp>

// The config of a vhost whose switches a test turns on: WebRTC and SRT on the server and the vhost, and every
// conversion off, on an origin.
class MockAppConfigForStreamBridges : public MockAppConfig
{
public:
    bool edge_;
    bool rtsp_enabled_;
    bool rtsp_from_rtmp_;

public:
    MockAppConfigForStreamBridges();
    virtual ~MockAppConfigForStreamBridges();

public:
    virtual bool get_vhost_is_edge(std::string vhost);
    virtual bool get_rtsp_server_enabled();
    virtual bool get_rtsp_enabled(std::string vhost);
    virtual bool get_rtsp_from_rtmp(std::string vhost);
};

// The source managers of one stream, as the server's are: each makes the real source of its protocol on the first
// fetch_or_create, and returns it after.
class MockRtmpSourcesForStreamBridges : public ISrsRtmpSourceManager
{
public:
    MockRtmpSourceHandler handler_;
    SrsSharedPtr<SrsRtmpSource> source_;

public:
    MockRtmpSourcesForStreamBridges();
    virtual ~MockRtmpSourcesForStreamBridges();

public:
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtmpSource> &pps);
    virtual SrsSharedPtr<SrsRtmpSource> fetch(ISrsRequest *r);
    virtual void dispose();
    virtual srs_error_t initialize();
};

class MockRtcSourcesForStreamBridges : public ISrsRtcSourceManager
{
public:
    SrsSharedPtr<SrsRtcSource> source_;

public:
    MockRtcSourcesForStreamBridges();
    virtual ~MockRtcSourcesForStreamBridges();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtcSource> &pps);
    virtual SrsSharedPtr<SrsRtcSource> fetch(ISrsRequest *r);
};

class MockSrtSourcesForStreamBridges : public ISrsSrtSourceManager
{
public:
    SrsSharedPtr<SrsSrtSource> source_;

public:
    MockSrtSourcesForStreamBridges();
    virtual ~MockSrtSourcesForStreamBridges();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsSrtSource> &pps);
    virtual SrsSharedPtr<SrsSrtSource> fetch(ISrsRequest *r);
};

#ifdef SRS_RTSP
class MockRtspSourcesForStreamBridges : public ISrsRtspSourceManager
{
public:
    SrsSharedPtr<SrsRtspSource> source_;

public:
    MockRtspSourcesForStreamBridges();
    virtual ~MockRtspSourcesForStreamBridges();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t fetch_or_create(ISrsRequest *r, SrsSharedPtr<SrsRtspSource> &pps);
    virtual SrsSharedPtr<SrsRtspSource> fetch(ISrsRequest *r);
};
#endif

#endif
