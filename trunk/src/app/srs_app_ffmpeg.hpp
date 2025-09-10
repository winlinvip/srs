//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FFMPEG_HPP
#define SRS_APP_FFMPEG_HPP

#include <srs_core.hpp>

#ifdef SRS_FFMPEG_STUB

#include <string>
#include <vector>

class SrsConfDirective;
class SrsPithyPrint;
class SrsProcess;

// A transcode engine: ffmepg, used to transcode a stream to another.
class SrsFFMPEG
{
private:
    SrsProcess *process_;
    std::vector<std::string> params_;

private:
    std::string log_file_;

private:
    std::string ffmpeg_;
    std::vector<std::string> iparams_;
    std::vector<std::string> perfile_;
    std::string iformat_;
    std::string input_;
    std::vector<std::string> vfilter_;
    std::string vcodec_;
    int vbitrate_;
    double vfps_;
    int vwidth_;
    int vheight_;
    int vthreads_;
    std::string vprofile_;
    std::string vpreset_;
    std::vector<std::string> vparams_;
    std::string acodec_;
    int abitrate_;
    int asample_rate_;
    int achannels_;
    std::vector<std::string> aparams_;
    std::string oformat_;
    std::string output_;

public:
    SrsFFMPEG(std::string ffmpeg_bin);
    virtual ~SrsFFMPEG();

public:
    virtual void append_iparam(std::string iparam);
    virtual void set_oformat(std::string format);
    virtual std::string output();

public:
    virtual srs_error_t initialize(std::string in, std::string out, std::string log);
    virtual srs_error_t initialize_transcode(SrsConfDirective *engine);
    virtual srs_error_t initialize_copy();

public:
    virtual srs_error_t start();
    virtual srs_error_t cycle();
    virtual void stop();

public:
    virtual void fast_stop();
    virtual void fast_kill();
};

#endif

#endif
