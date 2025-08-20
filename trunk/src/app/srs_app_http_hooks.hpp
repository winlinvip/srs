//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HTTP_HOOKS_HPP
#define SRS_APP_HTTP_HOOKS_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

class SrsHttpUri;
class SrsStSocket;
class SrsRequest;
class SrsHttpParser;
class SrsHttpClient;

class ISrsHttpHooks
{
public:
    ISrsHttpHooks();
    virtual ~ISrsHttpHooks();

public:
    // The on_connect hook, when client connect to srs.
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    virtual srs_error_t on_connect(std::string url, SrsRequest *req) = 0;
    // The on_close hook, when client disconnect to srs, where client is valid by on_connect.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    virtual void on_close(std::string url, SrsRequest *req, int64_t send_bytes, int64_t recv_bytes) = 0;
    // The on_publish hook, when client(encoder) start to publish stream
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    virtual srs_error_t on_publish(std::string url, SrsRequest *req) = 0;
    // The on_unpublish hook, when client(encoder) stop publish stream.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    virtual void on_unpublish(std::string url, SrsRequest *req) = 0;
    // The on_play hook, when client start to play stream.
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    virtual srs_error_t on_play(std::string url, SrsRequest *req) = 0;
    // The on_stop hook, when client stop to play the stream.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    virtual void on_stop(std::string url, SrsRequest *req) = 0;
    // The on_dvr hook, when reap a dvr file.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param file the file path, can be relative or absolute path.
    // @param cid the source connection cid, for the on_dvr is async call.
    virtual srs_error_t on_dvr(SrsContextId cid, std::string url, SrsRequest *req, std::string file) = 0;
    // When hls reap segment, callback.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param file the ts file path, can be relative or absolute path.
    // @param ts_url the ts url, which used for m3u8.
    // @param m3u8 the m3u8 file path, can be relative or absolute path.
    // @param m3u8_url the m3u8 url, which is used for the http mount path.
    // @param sn the seq_no, the sequence number of ts in hls/m3u8.
    // @param duration the segment duration in srs_utime_t.
    // @param cid the source connection cid, for the on_dvr is async call.
    virtual srs_error_t on_hls(SrsContextId cid, std::string url, SrsRequest *req, std::string file, std::string ts_url,
                       std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration) = 0;
    // When hls reap segment, callback.
    // @param url the api server url, to process the event.
    //         ignore if empty.
    // @param ts_url the ts uri, used to replace the variable [ts_url] in url.
    // @param nb_notify the max bytes to read from notify server.
    // @param cid the source connection cid, for the on_dvr is async call.
    virtual srs_error_t on_hls_notify(SrsContextId cid, std::string url, SrsRequest *req, std::string ts_url, int nb_notify) = 0;
    // Discover co-workers for origin cluster.
    virtual srs_error_t discover_co_workers(std::string url, std::string &host, int &port) = 0;
    // The on_forward_backend hook, when publish stream start to forward
    // @param url the api server url, to valid the client.
    //         ignore if empty.
    virtual srs_error_t on_forward_backend(std::string url, SrsRequest *req, std::vector<std::string> &rtmp_urls) = 0;
};

class SrsHttpHooks : public ISrsHttpHooks
{
public:
    SrsHttpHooks();
    virtual ~SrsHttpHooks();

public:
    srs_error_t on_connect(std::string url, SrsRequest *req);
    void on_close(std::string url, SrsRequest *req, int64_t send_bytes, int64_t recv_bytes);
    srs_error_t on_publish(std::string url, SrsRequest *req);
    void on_unpublish(std::string url, SrsRequest *req);
    srs_error_t on_play(std::string url, SrsRequest *req);
    void on_stop(std::string url, SrsRequest *req);
    srs_error_t on_dvr(SrsContextId cid, std::string url, SrsRequest *req, std::string file);
    srs_error_t on_hls(SrsContextId cid, std::string url, SrsRequest *req, std::string file, std::string ts_url,
                       std::string m3u8, std::string m3u8_url, int sn, srs_utime_t duration);
    srs_error_t on_hls_notify(SrsContextId cid, std::string url, SrsRequest *req, std::string ts_url, int nb_notify);
    srs_error_t discover_co_workers(std::string url, std::string &host, int &port);
    srs_error_t on_forward_backend(std::string url, SrsRequest *req, std::vector<std::string> &rtmp_urls);

private:
    srs_error_t do_post(SrsHttpClient *hc, std::string url, std::string req, int &code, std::string &res);
};

// Global HTTP hooks instance
extern ISrsHttpHooks* _srs_hooks;

#endif
