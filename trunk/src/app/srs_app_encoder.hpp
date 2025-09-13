//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_ENCODER_HPP
#define SRS_APP_ENCODER_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

#include <srs_app_st.hpp>

class SrsConfDirective;
class ISrsRequest;
class SrsPithyPrint;
class SrsFFMPEG;

// The encoder for a stream, may use multiple
// ffmpegs to transcode the specified stream.
class SrsEncoder : public ISrsCoroutineHandler
{
private:
    std::string input_stream_name_;
    std::vector<SrsFFMPEG *> ffmpegs_;

private:
    ISrsCoroutine *trd_;
    SrsPithyPrint *pprint_;

public:
    SrsEncoder();
    virtual ~SrsEncoder();

public:
    virtual srs_error_t on_publish(ISrsRequest *req);
    virtual void on_unpublish();
    // Interface ISrsReusableThreadHandler.
public:
    virtual srs_error_t cycle();

private:
    virtual srs_error_t do_cycle();

private:
    virtual void clear_engines();
    virtual SrsFFMPEG *at(int index);
    virtual srs_error_t parse_scope_engines(ISrsRequest *req);
    virtual srs_error_t parse_ffmpeg(ISrsRequest *req, SrsConfDirective *conf);
    virtual srs_error_t initialize_ffmpeg(SrsFFMPEG *ffmpeg, ISrsRequest *req, SrsConfDirective *engine);
    virtual void show_encode_log_message();
};

#endif
