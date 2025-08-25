//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_hybrid.hpp>

#include <srs_app_async_call.hpp>
#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_dvr.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_log.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_source.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_tencentcloud.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_st.hpp>
#ifdef SRS_SRT
#include <srs_app_srt_source.hpp>
#endif
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#ifdef SRS_RTSP
#include <srs_app_rtsp_source.hpp>
#endif
#include <srs_protocol_kbps.hpp>

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

SrsAsyncCallWorker *_srs_dvr_async = NULL;

extern SrsPps *_srs_pps_cids_get;
extern SrsPps *_srs_pps_cids_set;

extern SrsPps *_srs_pps_timer;
extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;
extern SrsPps *_srs_pps_dispose;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern __thread unsigned long long _st_stat_recvfrom;
extern __thread unsigned long long _st_stat_recvfrom_eagain;
extern __thread unsigned long long _st_stat_sendto;
extern __thread unsigned long long _st_stat_sendto_eagain;
SrsPps *_srs_pps_recvfrom = NULL;
SrsPps *_srs_pps_recvfrom_eagain = NULL;
SrsPps *_srs_pps_sendto = NULL;
SrsPps *_srs_pps_sendto_eagain = NULL;

extern __thread unsigned long long _st_stat_read;
extern __thread unsigned long long _st_stat_read_eagain;
extern __thread unsigned long long _st_stat_readv;
extern __thread unsigned long long _st_stat_readv_eagain;
extern __thread unsigned long long _st_stat_writev;
extern __thread unsigned long long _st_stat_writev_eagain;
SrsPps *_srs_pps_read = NULL;
SrsPps *_srs_pps_read_eagain = NULL;
SrsPps *_srs_pps_readv = NULL;
SrsPps *_srs_pps_readv_eagain = NULL;
SrsPps *_srs_pps_writev = NULL;
SrsPps *_srs_pps_writev_eagain = NULL;

extern __thread unsigned long long _st_stat_recvmsg;
extern __thread unsigned long long _st_stat_recvmsg_eagain;
extern __thread unsigned long long _st_stat_sendmsg;
extern __thread unsigned long long _st_stat_sendmsg_eagain;
SrsPps *_srs_pps_recvmsg = NULL;
SrsPps *_srs_pps_recvmsg_eagain = NULL;
SrsPps *_srs_pps_sendmsg = NULL;
SrsPps *_srs_pps_sendmsg_eagain = NULL;

extern __thread unsigned long long _st_stat_epoll;
extern __thread unsigned long long _st_stat_epoll_zero;
extern __thread unsigned long long _st_stat_epoll_shake;
extern __thread unsigned long long _st_stat_epoll_spin;
SrsPps *_srs_pps_epoll = NULL;
SrsPps *_srs_pps_epoll_zero = NULL;
SrsPps *_srs_pps_epoll_shake = NULL;
SrsPps *_srs_pps_epoll_spin = NULL;

extern __thread unsigned long long _st_stat_sched_15ms;
extern __thread unsigned long long _st_stat_sched_20ms;
extern __thread unsigned long long _st_stat_sched_25ms;
extern __thread unsigned long long _st_stat_sched_30ms;
extern __thread unsigned long long _st_stat_sched_35ms;
extern __thread unsigned long long _st_stat_sched_40ms;
extern __thread unsigned long long _st_stat_sched_80ms;
extern __thread unsigned long long _st_stat_sched_160ms;
extern __thread unsigned long long _st_stat_sched_s;
SrsPps *_srs_pps_sched_15ms = NULL;
SrsPps *_srs_pps_sched_20ms = NULL;
SrsPps *_srs_pps_sched_25ms = NULL;
SrsPps *_srs_pps_sched_30ms = NULL;
SrsPps *_srs_pps_sched_35ms = NULL;
SrsPps *_srs_pps_sched_40ms = NULL;
SrsPps *_srs_pps_sched_80ms = NULL;
SrsPps *_srs_pps_sched_160ms = NULL;
SrsPps *_srs_pps_sched_s = NULL;
#endif

SrsPps *_srs_pps_clock_15ms = NULL;
SrsPps *_srs_pps_clock_20ms = NULL;
SrsPps *_srs_pps_clock_25ms = NULL;
SrsPps *_srs_pps_clock_30ms = NULL;
SrsPps *_srs_pps_clock_35ms = NULL;
SrsPps *_srs_pps_clock_40ms = NULL;
SrsPps *_srs_pps_clock_80ms = NULL;
SrsPps *_srs_pps_clock_160ms = NULL;
SrsPps *_srs_pps_timer_s = NULL;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern __thread int _st_active_count;
extern __thread int _st_num_free_stacks;
extern __thread unsigned long long _st_stat_thread_run;
extern __thread unsigned long long _st_stat_thread_idle;
extern __thread unsigned long long _st_stat_thread_yield;
extern __thread unsigned long long _st_stat_thread_yield2;
SrsPps *_srs_pps_thread_run = NULL;
SrsPps *_srs_pps_thread_idle = NULL;
SrsPps *_srs_pps_thread_yield = NULL;
SrsPps *_srs_pps_thread_yield2 = NULL;
#endif

extern SrsPps *_srs_pps_objs_rtps;
extern SrsPps *_srs_pps_objs_rraw;
extern SrsPps *_srs_pps_objs_rfua;
extern SrsPps *_srs_pps_objs_rbuf;
extern SrsPps *_srs_pps_objs_msgs;
extern SrsPps *_srs_pps_objs_rothers;

extern ISrsLog *_srs_log;
extern ISrsContext *_srs_context;
extern SrsConfig *_srs_config;

extern SrsStageManager *_srs_stages;

extern SrsRtcBlackhole *_srs_blackhole;
extern SrsResourceManager *_srs_rtc_manager;

extern SrsDtlsCertificate *_srs_rtc_dtls_certificate;

SrsPps *_srs_pps_aloss2 = NULL;

extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;
extern SrsPps *_srs_pps_dispose;

extern SrsPps *_srs_pps_timer;

extern SrsPps *_srs_pps_snack;
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_snack3;
extern SrsPps *_srs_pps_snack4;
extern SrsPps *_srs_pps_sanack;
extern SrsPps *_srs_pps_svnack;

extern SrsPps *_srs_pps_rnack;
extern SrsPps *_srs_pps_rnack2;
extern SrsPps *_srs_pps_rhnack;
extern SrsPps *_srs_pps_rmnack;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps *_srs_pps_recvfrom;
extern SrsPps *_srs_pps_recvfrom_eagain;
extern SrsPps *_srs_pps_sendto;
extern SrsPps *_srs_pps_sendto_eagain;

extern SrsPps *_srs_pps_read;
extern SrsPps *_srs_pps_read_eagain;
extern SrsPps *_srs_pps_readv;
extern SrsPps *_srs_pps_readv_eagain;
extern SrsPps *_srs_pps_writev;
extern SrsPps *_srs_pps_writev_eagain;

extern SrsPps *_srs_pps_recvmsg;
extern SrsPps *_srs_pps_recvmsg_eagain;
extern SrsPps *_srs_pps_sendmsg;
extern SrsPps *_srs_pps_sendmsg_eagain;

extern SrsPps *_srs_pps_epoll;
extern SrsPps *_srs_pps_epoll_zero;
extern SrsPps *_srs_pps_epoll_shake;
extern SrsPps *_srs_pps_epoll_spin;

extern SrsPps *_srs_pps_sched_15ms;
extern SrsPps *_srs_pps_sched_20ms;
extern SrsPps *_srs_pps_sched_25ms;
extern SrsPps *_srs_pps_sched_30ms;
extern SrsPps *_srs_pps_sched_35ms;
extern SrsPps *_srs_pps_sched_40ms;
extern SrsPps *_srs_pps_sched_80ms;
extern SrsPps *_srs_pps_sched_160ms;
extern SrsPps *_srs_pps_sched_s;
#endif

extern SrsPps *_srs_pps_clock_15ms;
extern SrsPps *_srs_pps_clock_20ms;
extern SrsPps *_srs_pps_clock_25ms;
extern SrsPps *_srs_pps_clock_30ms;
extern SrsPps *_srs_pps_clock_35ms;
extern SrsPps *_srs_pps_clock_40ms;
extern SrsPps *_srs_pps_clock_80ms;
extern SrsPps *_srs_pps_clock_160ms;
extern SrsPps *_srs_pps_timer_s;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
extern SrsPps *_srs_pps_thread_run;
extern SrsPps *_srs_pps_thread_idle;
extern SrsPps *_srs_pps_thread_yield;
extern SrsPps *_srs_pps_thread_yield2;
#endif

extern SrsPps *_srs_pps_rpkts;
extern SrsPps *_srs_pps_addrs;
extern SrsPps *_srs_pps_fast_addrs;

extern SrsPps *_srs_pps_spkts;

extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

extern SrsPps *_srs_pps_pli;
extern SrsPps *_srs_pps_twcc;
extern SrsPps *_srs_pps_rr;
extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;

extern SrsPps *_srs_pps_rstuns;
extern SrsPps *_srs_pps_rrtps;
extern SrsPps *_srs_pps_rrtcps;

extern SrsPps *_srs_pps_aloss2;

extern SrsPps *_srs_pps_cids_get;
extern SrsPps *_srs_pps_cids_set;

extern SrsPps *_srs_pps_objs_msgs;

extern SrsPps *_srs_pps_objs_rtps;
extern SrsPps *_srs_pps_objs_rraw;
extern SrsPps *_srs_pps_objs_rfua;
extern SrsPps *_srs_pps_objs_rbuf;
extern SrsPps *_srs_pps_objs_rothers;

extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

ISrsHybridServer::ISrsHybridServer()
{
}

ISrsHybridServer::~ISrsHybridServer()
{
}

SrsHybridServer::SrsHybridServer()
{
    // Create global shared timer.
    timer20ms_ = new SrsFastTimer("hybrid", 20 * SRS_UTIME_MILLISECONDS);
    timer100ms_ = new SrsFastTimer("hybrid", 100 * SRS_UTIME_MILLISECONDS);
    timer1s_ = new SrsFastTimer("hybrid", 1 * SRS_UTIME_SECONDS);
    timer5s_ = new SrsFastTimer("hybrid", 5 * SRS_UTIME_SECONDS);

    clock_monitor_ = new SrsClockWallMonitor();

    pid_fd = -1;
}

SrsHybridServer::~SrsHybridServer()
{
    // We must free servers first, because it may depend on the timers of hybrid server.
    vector<ISrsHybridServer *>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer *server = *it;
        srs_freep(server);
    }
    servers.clear();

    srs_freep(clock_monitor_);

    srs_freep(timer20ms_);
    srs_freep(timer100ms_);
    srs_freep(timer1s_);
    srs_freep(timer5s_);

    if (pid_fd > 0) {
        ::close(pid_fd);
        pid_fd = -1;
    }
}

void SrsHybridServer::register_server(ISrsHybridServer *svr)
{
    servers.push_back(svr);
}

srs_error_t SrsHybridServer::initialize()
{
    srs_error_t err = srs_success;

    if ((err = acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "acquire pid file");
    }

    srs_trace("Hybrid server initialized in single thread mode");

    // Start the timer first.
    if ((err = timer20ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer100ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer1s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    if ((err = timer5s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer");
    }

    // Start the DVR async call.
    if ((err = _srs_dvr_async->start()) != srs_success) {
        return srs_error_wrap(err, "dvr async");
    }

#ifdef SRS_APM
    // Initialize TencentCloud CLS object.
    if ((err = _srs_cls->initialize()) != srs_success) {
        return srs_error_wrap(err, "cls client");
    }
    if ((err = _srs_apm->initialize()) != srs_success) {
        return srs_error_wrap(err, "apm client");
    }
#endif

    // Register some timers.
    timer20ms_->subscribe(clock_monitor_);
    timer5s_->subscribe(this);

    // Initialize all hybrid servers.
    vector<ISrsHybridServer *>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer *server = *it;

        if ((err = server->initialize()) != srs_success) {
            return srs_error_wrap(err, "init server");
        }
    }

    return err;
}

srs_error_t SrsHybridServer::run()
{
    srs_error_t err = srs_success;

    // Wait for all servers which need to do cleanup.
    SrsWaitGroup wg;

    vector<ISrsHybridServer *>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer *server = *it;

        if ((err = server->run(&wg)) != srs_success) {
            return srs_error_wrap(err, "run server");
        }
    }

    // Wait for all server to quit.
    wg.wait();

    return err;
}

void SrsHybridServer::stop()
{
    vector<ISrsHybridServer *>::iterator it;
    for (it = servers.begin(); it != servers.end(); ++it) {
        ISrsHybridServer *server = *it;
        server->stop();
    }
}

SrsServerAdapter *SrsHybridServer::srs()
{
    for (vector<ISrsHybridServer *>::iterator it = servers.begin(); it != servers.end(); ++it) {
        if (dynamic_cast<SrsServerAdapter *>(*it)) {
            return dynamic_cast<SrsServerAdapter *>(*it);
        }
    }
    return NULL;
}

SrsFastTimer *SrsHybridServer::timer20ms()
{
    return timer20ms_;
}

SrsFastTimer *SrsHybridServer::timer100ms()
{
    return timer100ms_;
}

SrsFastTimer *SrsHybridServer::timer1s()
{
    return timer1s_;
}

SrsFastTimer *SrsHybridServer::timer5s()
{
    return timer5s_;
}

srs_error_t SrsHybridServer::on_timer(srs_utime_t interval)
{
    srs_error_t err = srs_success;

    // Show statistics for RTC server.
    SrsProcSelfStat *u = srs_get_self_proc_stat();
    // Resident Set Size: number of pages the process has in real memory.
    int memory = (int)(u->rss * 4 / 1024);

    static char buf[128];

    string cid_desc;
    _srs_pps_cids_get->update();
    _srs_pps_cids_set->update();
    if (_srs_pps_cids_get->r10s() || _srs_pps_cids_set->r10s()) {
        snprintf(buf, sizeof(buf), ", cid=%d,%d", _srs_pps_cids_get->r10s(), _srs_pps_cids_set->r10s());
        cid_desc = buf;
    }

    string timer_desc;
    _srs_pps_timer->update();
    _srs_pps_pub->update();
    _srs_pps_conn->update();
    if (_srs_pps_timer->r10s() || _srs_pps_pub->r10s() || _srs_pps_conn->r10s()) {
        snprintf(buf, sizeof(buf), ", timer=%d,%d,%d", _srs_pps_timer->r10s(), _srs_pps_pub->r10s(), _srs_pps_conn->r10s());
        timer_desc = buf;
    }

    string free_desc;
    _srs_pps_dispose->update();
    if (_srs_pps_dispose->r10s()) {
        snprintf(buf, sizeof(buf), ", free=%d", _srs_pps_dispose->r10s());
        free_desc = buf;
    }

    string recvfrom_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom->update(_st_stat_recvfrom);
    _srs_pps_recvfrom_eagain->update(_st_stat_recvfrom_eagain);
    _srs_pps_sendto->update(_st_stat_sendto);
    _srs_pps_sendto_eagain->update(_st_stat_sendto_eagain);
    if (_srs_pps_recvfrom->r10s() || _srs_pps_recvfrom_eagain->r10s() || _srs_pps_sendto->r10s() || _srs_pps_sendto_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", udp=%d,%d,%d,%d", _srs_pps_recvfrom->r10s(), _srs_pps_recvfrom_eagain->r10s(), _srs_pps_sendto->r10s(), _srs_pps_sendto_eagain->r10s());
        recvfrom_desc = buf;
    }
#endif

    string io_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_read->update(_st_stat_read);
    _srs_pps_read_eagain->update(_st_stat_read_eagain);
    _srs_pps_readv->update(_st_stat_readv);
    _srs_pps_readv_eagain->update(_st_stat_readv_eagain);
    _srs_pps_writev->update(_st_stat_writev);
    _srs_pps_writev_eagain->update(_st_stat_writev_eagain);
    if (_srs_pps_read->r10s() || _srs_pps_read_eagain->r10s() || _srs_pps_readv->r10s() || _srs_pps_readv_eagain->r10s() || _srs_pps_writev->r10s() || _srs_pps_writev_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", io=%d,%d,%d,%d,%d,%d", _srs_pps_read->r10s(), _srs_pps_read_eagain->r10s(), _srs_pps_readv->r10s(), _srs_pps_readv_eagain->r10s(), _srs_pps_writev->r10s(), _srs_pps_writev_eagain->r10s());
        io_desc = buf;
    }
#endif

    string msg_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvmsg->update(_st_stat_recvmsg);
    _srs_pps_recvmsg_eagain->update(_st_stat_recvmsg_eagain);
    _srs_pps_sendmsg->update(_st_stat_sendmsg);
    _srs_pps_sendmsg_eagain->update(_st_stat_sendmsg_eagain);
    if (_srs_pps_recvmsg->r10s() || _srs_pps_recvmsg_eagain->r10s() || _srs_pps_sendmsg->r10s() || _srs_pps_sendmsg_eagain->r10s()) {
        snprintf(buf, sizeof(buf), ", msg=%d,%d,%d,%d", _srs_pps_recvmsg->r10s(), _srs_pps_recvmsg_eagain->r10s(), _srs_pps_sendmsg->r10s(), _srs_pps_sendmsg_eagain->r10s());
        msg_desc = buf;
    }
#endif

    string epoll_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_epoll->update(_st_stat_epoll);
    _srs_pps_epoll_zero->update(_st_stat_epoll_zero);
    _srs_pps_epoll_shake->update(_st_stat_epoll_shake);
    _srs_pps_epoll_spin->update(_st_stat_epoll_spin);
    if (_srs_pps_epoll->r10s() || _srs_pps_epoll_zero->r10s() || _srs_pps_epoll_shake->r10s() || _srs_pps_epoll_spin->r10s()) {
        snprintf(buf, sizeof(buf), ", epoll=%d,%d,%d,%d", _srs_pps_epoll->r10s(), _srs_pps_epoll_zero->r10s(), _srs_pps_epoll_shake->r10s(), _srs_pps_epoll_spin->r10s());
        epoll_desc = buf;
    }
#endif

    string sched_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_sched_160ms->update(_st_stat_sched_160ms);
    _srs_pps_sched_s->update(_st_stat_sched_s);
    _srs_pps_sched_15ms->update(_st_stat_sched_15ms);
    _srs_pps_sched_20ms->update(_st_stat_sched_20ms);
    _srs_pps_sched_25ms->update(_st_stat_sched_25ms);
    _srs_pps_sched_30ms->update(_st_stat_sched_30ms);
    _srs_pps_sched_35ms->update(_st_stat_sched_35ms);
    _srs_pps_sched_40ms->update(_st_stat_sched_40ms);
    _srs_pps_sched_80ms->update(_st_stat_sched_80ms);
    if (_srs_pps_sched_160ms->r10s() || _srs_pps_sched_s->r10s() || _srs_pps_sched_15ms->r10s() || _srs_pps_sched_20ms->r10s() || _srs_pps_sched_25ms->r10s() || _srs_pps_sched_30ms->r10s() || _srs_pps_sched_35ms->r10s() || _srs_pps_sched_40ms->r10s() || _srs_pps_sched_80ms->r10s()) {
        snprintf(buf, sizeof(buf), ", sched=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_sched_15ms->r10s(), _srs_pps_sched_20ms->r10s(), _srs_pps_sched_25ms->r10s(), _srs_pps_sched_30ms->r10s(), _srs_pps_sched_35ms->r10s(), _srs_pps_sched_40ms->r10s(), _srs_pps_sched_80ms->r10s(), _srs_pps_sched_160ms->r10s(), _srs_pps_sched_s->r10s());
        sched_desc = buf;
    }
#endif

    string clock_desc;
    _srs_pps_clock_15ms->update();
    _srs_pps_clock_20ms->update();
    _srs_pps_clock_25ms->update();
    _srs_pps_clock_30ms->update();
    _srs_pps_clock_35ms->update();
    _srs_pps_clock_40ms->update();
    _srs_pps_clock_80ms->update();
    _srs_pps_clock_160ms->update();
    _srs_pps_timer_s->update();
    if (_srs_pps_clock_15ms->r10s() || _srs_pps_timer_s->r10s() || _srs_pps_clock_20ms->r10s() || _srs_pps_clock_25ms->r10s() || _srs_pps_clock_30ms->r10s() || _srs_pps_clock_35ms->r10s() || _srs_pps_clock_40ms->r10s() || _srs_pps_clock_80ms->r10s() || _srs_pps_clock_160ms->r10s()) {
        snprintf(buf, sizeof(buf), ", clock=%d,%d,%d,%d,%d,%d,%d,%d,%d", _srs_pps_clock_15ms->r10s(), _srs_pps_clock_20ms->r10s(), _srs_pps_clock_25ms->r10s(), _srs_pps_clock_30ms->r10s(), _srs_pps_clock_35ms->r10s(), _srs_pps_clock_40ms->r10s(), _srs_pps_clock_80ms->r10s(), _srs_pps_clock_160ms->r10s(), _srs_pps_timer_s->r10s());
        clock_desc = buf;
    }

    string thread_desc;
#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run->update(_st_stat_thread_run);
    _srs_pps_thread_idle->update(_st_stat_thread_idle);
    _srs_pps_thread_yield->update(_st_stat_thread_yield);
    _srs_pps_thread_yield2->update(_st_stat_thread_yield2);
    if (_st_active_count > 0 || _st_num_free_stacks > 0 || _srs_pps_thread_run->r10s() || _srs_pps_thread_idle->r10s() || _srs_pps_thread_yield->r10s() || _srs_pps_thread_yield2->r10s()) {
        snprintf(buf, sizeof(buf), ", co=%d,%d,%d, stk=%d, yield=%d,%d", _st_active_count, _srs_pps_thread_run->r10s(), _srs_pps_thread_idle->r10s(), _st_num_free_stacks, _srs_pps_thread_yield->r10s(), _srs_pps_thread_yield2->r10s());
        thread_desc = buf;
    }
#endif

    string objs_desc;
    _srs_pps_objs_rtps->update();
    _srs_pps_objs_rraw->update();
    _srs_pps_objs_rfua->update();
    _srs_pps_objs_rbuf->update();
    _srs_pps_objs_msgs->update();
    _srs_pps_objs_rothers->update();
    if (_srs_pps_objs_rtps->r10s() || _srs_pps_objs_rraw->r10s() || _srs_pps_objs_rfua->r10s() || _srs_pps_objs_rbuf->r10s() || _srs_pps_objs_msgs->r10s() || _srs_pps_objs_rothers->r10s()) {
        snprintf(buf, sizeof(buf), ", objs=(pkt:%d,raw:%d,fua:%d,msg:%d,oth:%d,buf:%d)",
                 _srs_pps_objs_rtps->r10s(), _srs_pps_objs_rraw->r10s(), _srs_pps_objs_rfua->r10s(),
                 _srs_pps_objs_msgs->r10s(), _srs_pps_objs_rothers->r10s(), _srs_pps_objs_rbuf->r10s());
        objs_desc = buf;
    }

    srs_trace("Hybrid cpu=%.2f%%,%dMB%s%s%s%s%s%s%s%s%s%s%s",
              u->percent * 100, memory,
              cid_desc.c_str(), timer_desc.c_str(),
              recvfrom_desc.c_str(), io_desc.c_str(), msg_desc.c_str(),
              epoll_desc.c_str(), sched_desc.c_str(), clock_desc.c_str(),
              thread_desc.c_str(), free_desc.c_str(), objs_desc.c_str());

#ifdef SRS_APM
    // Report logs to CLS if enabled.
    if ((err = _srs_cls->report()) != srs_success) {
        srs_warn("ignore cls err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }

    // Report logs to APM if enabled.
    if ((err = _srs_apm->report()) != srs_success) {
        srs_warn("ignore apm err %s", srs_error_desc(err).c_str());
        srs_freep(err);
    }
#endif

    return err;
}

srs_error_t SrsHybridServer::acquire_pid_file()
{
    std::string pid_file = _srs_config->get_pid_file();

    // -rw-r--r--
    // 644
    int mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    int fd;
    // open pid file
    if ((fd = ::open(pid_file.c_str(), O_WRONLY | O_CREAT, mode)) == -1) {
        return srs_error_new(ERROR_SYSTEM_PID_ACQUIRE, "open pid file=%s", pid_file.c_str());
    }

    // require write lock
    struct flock lock;

    lock.l_type = F_WRLCK;    // F_RDLCK, F_WRLCK, F_UNLCK
    lock.l_start = 0;         // type offset, relative to l_whence
    lock.l_whence = SEEK_SET; // SEEK_SET, SEEK_CUR, SEEK_END
    lock.l_len = 0;

    if (fcntl(fd, F_SETLK, &lock) == -1) {
        if (errno == EACCES || errno == EAGAIN) {
            ::close(fd);
            srs_error("srs is already running!");
            return srs_error_new(ERROR_SYSTEM_PID_ALREADY_RUNNING, "srs is already running");
        }
        return srs_error_new(ERROR_SYSTEM_PID_LOCK, "access to pid=%s", pid_file.c_str());
    }

    // truncate file
    if (ftruncate(fd, 0) != 0) {
        return srs_error_new(ERROR_SYSTEM_PID_TRUNCATE_FILE, "truncate pid file=%s", pid_file.c_str());
    }

    // write the pid
    string pid = srs_int2str(getpid());
    if (write(fd, pid.c_str(), pid.length()) != (int)pid.length()) {
        return srs_error_new(ERROR_SYSTEM_PID_WRITE_FILE, "write pid=%s to file=%s", pid.c_str(), pid_file.c_str());
    }

    // auto close when fork child process.
    int val;
    if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fcntl fd=%d", fd);
    }
    val |= FD_CLOEXEC;
    if (fcntl(fd, F_SETFD, val) < 0) {
        return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "lock file=%s fd=%d", pid_file.c_str(), fd);
    }

    srs_trace("write pid=%s to %s success!", pid.c_str(), pid_file.c_str());
    pid_fd = fd;

    return srs_success;
}

srs_error_t srs_global_initialize()
{
    srs_error_t err = srs_success;

    // Root global objects.
    _srs_log = new SrsFileLog();
    _srs_context = new SrsThreadContext();
    _srs_config = new SrsConfig();

    // The clock wall object.
    _srs_clock = new SrsWallClock();

    // The pps cids depends by st init.
    _srs_pps_cids_get = new SrsPps();
    _srs_pps_cids_set = new SrsPps();

    // Initialize ST, which depends on pps cids.
    if ((err = srs_st_init()) != srs_success) {
        return srs_error_wrap(err, "initialize st failed");
    }

    // The global objects which depends on ST.
    _srs_hybrid = new SrsHybridServer();
    _srs_sources = new SrsLiveSourceManager();
    _srs_stages = new SrsStageManager();
    _srs_circuit_breaker = new SrsCircuitBreaker();
    _srs_hooks = new SrsHttpHooks();

#ifdef SRS_SRT
    _srs_srt_sources = new SrsSrtSourceManager();
#endif

    _srs_rtc_sources = new SrsRtcSourceManager();
    _srs_blackhole = new SrsRtcBlackhole();

    // Initialize stream publish token manager
    _srs_stream_publish_tokens = new SrsStreamPublishTokenManager();

    _srs_rtc_manager = new SrsResourceManager("RTC", true);
    _srs_rtc_dtls_certificate = new SrsDtlsCertificate();
#ifdef SRS_RTSP
    _srs_rtsp_sources = new SrsRtspSourceManager();
    _srs_rtsp_manager = new SrsResourceManager("RTSP", true);
#endif
#ifdef SRS_GB28181
    _srs_gb_manager = new SrsResourceManager("GB", true);
#endif

    // Initialize global pps, which depends on _srs_clock
    _srs_pps_ids = new SrsPps();
    _srs_pps_fids = new SrsPps();
    _srs_pps_fids_level0 = new SrsPps();
    _srs_pps_dispose = new SrsPps();

    _srs_pps_timer = new SrsPps();
    _srs_pps_conn = new SrsPps();
    _srs_pps_pub = new SrsPps();

    _srs_pps_snack = new SrsPps();
    _srs_pps_snack2 = new SrsPps();
    _srs_pps_snack3 = new SrsPps();
    _srs_pps_snack4 = new SrsPps();
    _srs_pps_sanack = new SrsPps();
    _srs_pps_svnack = new SrsPps();

    _srs_pps_rnack = new SrsPps();
    _srs_pps_rnack2 = new SrsPps();
    _srs_pps_rhnack = new SrsPps();
    _srs_pps_rmnack = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_recvfrom = new SrsPps();
    _srs_pps_recvfrom_eagain = new SrsPps();
    _srs_pps_sendto = new SrsPps();
    _srs_pps_sendto_eagain = new SrsPps();

    _srs_pps_read = new SrsPps();
    _srs_pps_read_eagain = new SrsPps();
    _srs_pps_readv = new SrsPps();
    _srs_pps_readv_eagain = new SrsPps();
    _srs_pps_writev = new SrsPps();
    _srs_pps_writev_eagain = new SrsPps();

    _srs_pps_recvmsg = new SrsPps();
    _srs_pps_recvmsg_eagain = new SrsPps();
    _srs_pps_sendmsg = new SrsPps();
    _srs_pps_sendmsg_eagain = new SrsPps();

    _srs_pps_epoll = new SrsPps();
    _srs_pps_epoll_zero = new SrsPps();
    _srs_pps_epoll_shake = new SrsPps();
    _srs_pps_epoll_spin = new SrsPps();

    _srs_pps_sched_15ms = new SrsPps();
    _srs_pps_sched_20ms = new SrsPps();
    _srs_pps_sched_25ms = new SrsPps();
    _srs_pps_sched_30ms = new SrsPps();
    _srs_pps_sched_35ms = new SrsPps();
    _srs_pps_sched_40ms = new SrsPps();
    _srs_pps_sched_80ms = new SrsPps();
    _srs_pps_sched_160ms = new SrsPps();
    _srs_pps_sched_s = new SrsPps();
#endif

    _srs_pps_clock_15ms = new SrsPps();
    _srs_pps_clock_20ms = new SrsPps();
    _srs_pps_clock_25ms = new SrsPps();
    _srs_pps_clock_30ms = new SrsPps();
    _srs_pps_clock_35ms = new SrsPps();
    _srs_pps_clock_40ms = new SrsPps();
    _srs_pps_clock_80ms = new SrsPps();
    _srs_pps_clock_160ms = new SrsPps();
    _srs_pps_timer_s = new SrsPps();

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
    _srs_pps_thread_run = new SrsPps();
    _srs_pps_thread_idle = new SrsPps();
    _srs_pps_thread_yield = new SrsPps();
    _srs_pps_thread_yield2 = new SrsPps();
#endif

    _srs_pps_rpkts = new SrsPps();
    _srs_pps_addrs = new SrsPps();
    _srs_pps_fast_addrs = new SrsPps();

    _srs_pps_spkts = new SrsPps();
    _srs_pps_objs_msgs = new SrsPps();

    _srs_pps_sstuns = new SrsPps();
    _srs_pps_srtcps = new SrsPps();
    _srs_pps_srtps = new SrsPps();

    _srs_pps_rstuns = new SrsPps();
    _srs_pps_rrtps = new SrsPps();
    _srs_pps_rrtcps = new SrsPps();

    _srs_pps_aloss2 = new SrsPps();

    _srs_pps_pli = new SrsPps();
    _srs_pps_twcc = new SrsPps();
    _srs_pps_rr = new SrsPps();

    _srs_pps_objs_rtps = new SrsPps();
    _srs_pps_objs_rraw = new SrsPps();
    _srs_pps_objs_rfua = new SrsPps();
    _srs_pps_objs_rbuf = new SrsPps();
    _srs_pps_objs_rothers = new SrsPps();

    // Create global async worker for DVR.
    _srs_dvr_async = new SrsAsyncCallWorker();

#ifdef SRS_APM
    // Initialize global TencentCloud CLS object.
    _srs_cls = new SrsClsClient();
    _srs_apm = new SrsApmClient();
#endif

    _srs_reload_err = srs_success;
    _srs_reload_state = SrsReloadStateInit;
    _srs_reload_id = srs_random_str(7);

    return err;
}

SrsHybridServer *_srs_hybrid = NULL;
