//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_HYBRID_HPP
#define SRS_APP_HYBRID_HPP

#include <srs_core.hpp>

#include <vector>

#include <srs_app_hourglass.hpp>

class SrsServer;
class SrsWaitGroup;

// Initialize global shared variables cross all threads.
extern srs_error_t srs_global_initialize();

// The hybrid server manager.
class SrsHybridServer : public ISrsFastTimer
{
private:
    SrsServer *server_;
    SrsFastTimer *timer20ms_;
    SrsFastTimer *timer100ms_;
    SrsFastTimer *timer1s_;
    SrsFastTimer *timer5s_;
    SrsClockWallMonitor *clock_monitor_;

private:
    // The pid file fd, lock the file write when server is running.
    // @remark the init.d script should cleanup the pid file, when stop service,
    //       for the server never delete the file; when system startup, the pid in pid file
    //       maybe valid but the process is not SRS, the init.d script will never start server.
    int pid_fd;

public:
    SrsHybridServer();
    virtual ~SrsHybridServer();

public:
    virtual srs_error_t initialize();
    virtual srs_error_t run();
    virtual void stop();

public:
    virtual SrsServer *srs();
    SrsFastTimer *timer20ms();
    SrsFastTimer *timer100ms();
    SrsFastTimer *timer1s();
    SrsFastTimer *timer5s();
    // interface ISrsFastTimer
private:
    srs_error_t on_timer(srs_utime_t interval);

private:
    // Require the PID file for the whole process.
    virtual srs_error_t acquire_pid_file();
};

extern SrsHybridServer *_srs_hybrid;

#endif
