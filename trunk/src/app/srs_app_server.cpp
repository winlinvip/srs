//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_server.hpp>

#include <algorithm>
#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
#include <sys/inotify.h>
#endif
using namespace std;

#include <srs_app_async_call.hpp>
#include <srs_app_caster_flv.hpp>
#include <srs_app_circuit_breaker.hpp>
#include <srs_app_config.hpp>
#include <srs_app_conn.hpp>
#include <srs_app_coworkers.hpp>
#include <srs_app_heartbeat.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_http_conn.hpp>
#include <srs_app_http_hooks.hpp>
#include <srs_app_ingest.hpp>
#include <srs_app_latest_version.hpp>
#include <srs_app_log.hpp>
#include <srs_app_mpegts_udp.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_reload.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_sdp.hpp>
#include <srs_app_rtc_server.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_rtmp_conn.hpp>
#include <srs_app_source.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_stream_token.hpp>
#include <srs_app_utility.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#ifdef SRS_GB28181
#include <srs_app_gb28181.hpp>
#endif
#ifdef SRS_SRT
#include <srs_app_srt_conn.hpp>
#include <srs_app_srt_server.hpp>
#include <srs_app_srt_source.hpp>
#endif
#ifdef SRS_RTSP
#include <srs_app_rtsp_conn.hpp>
#include <srs_app_rtsp_source.hpp>
#endif

SrsServer *_srs_server = NULL;

SrsAsyncCallWorker *_srs_dvr_async = NULL;

SrsPps *_srs_pps_recvfrom = NULL;
SrsPps *_srs_pps_recvfrom_eagain = NULL;
SrsPps *_srs_pps_sendto = NULL;
SrsPps *_srs_pps_sendto_eagain = NULL;

SrsPps *_srs_pps_read = NULL;
SrsPps *_srs_pps_read_eagain = NULL;
SrsPps *_srs_pps_readv = NULL;
SrsPps *_srs_pps_readv_eagain = NULL;
SrsPps *_srs_pps_writev = NULL;
SrsPps *_srs_pps_writev_eagain = NULL;

SrsPps *_srs_pps_recvmsg = NULL;
SrsPps *_srs_pps_recvmsg_eagain = NULL;
SrsPps *_srs_pps_sendmsg = NULL;
SrsPps *_srs_pps_sendmsg_eagain = NULL;

SrsPps *_srs_pps_clock_15ms = NULL;
SrsPps *_srs_pps_clock_20ms = NULL;
SrsPps *_srs_pps_clock_25ms = NULL;
SrsPps *_srs_pps_clock_30ms = NULL;
SrsPps *_srs_pps_clock_35ms = NULL;
SrsPps *_srs_pps_clock_40ms = NULL;
SrsPps *_srs_pps_clock_80ms = NULL;
SrsPps *_srs_pps_clock_160ms = NULL;
SrsPps *_srs_pps_timer_s = NULL;

// External declarations for WebRTC functions and variables
extern bool srs_is_stun(const uint8_t *data, size_t size);
extern bool srs_is_dtls(const uint8_t *data, size_t len);
extern bool srs_is_rtp_or_rtcp(const uint8_t *data, size_t len);
extern bool srs_is_rtcp(const uint8_t *data, size_t len);

extern SrsPps *_srs_pps_rpkts;
SrsPps *_srs_pps_rstuns = NULL;
SrsPps *_srs_pps_rrtps = NULL;
SrsPps *_srs_pps_rrtcps = NULL;
extern SrsPps *_srs_pps_addrs;
extern SrsPps *_srs_pps_fast_addrs;

extern SrsPps *_srs_pps_spkts;
extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;
extern SrsPps *_srs_pps_dispose;

extern SrsPps *_srs_pps_timer;
extern SrsPps *_srs_pps_pub;
extern SrsPps *_srs_pps_conn;

extern SrsPps *_srs_pps_cids_get;
extern SrsPps *_srs_pps_cids_set;

extern SrsPps *_srs_pps_snack3;
extern SrsPps *_srs_pps_snack4;
extern SrsPps *_srs_pps_aloss2;

extern SrsStageManager *_srs_stages;

extern srs_error_t _srs_reload_err;
extern SrsReloadState _srs_reload_state;
extern std::string _srs_reload_id;

// Clock and timing statistics
extern SrsPps *_srs_pps_clock_15ms;
extern SrsPps *_srs_pps_clock_20ms;
extern SrsPps *_srs_pps_clock_25ms;
extern SrsPps *_srs_pps_clock_30ms;
extern SrsPps *_srs_pps_clock_35ms;
extern SrsPps *_srs_pps_clock_40ms;
extern SrsPps *_srs_pps_clock_80ms;
extern SrsPps *_srs_pps_clock_160ms;
extern SrsPps *_srs_pps_timer_s;

// Object statistics
extern SrsPps *_srs_pps_objs_rtps;
extern SrsPps *_srs_pps_objs_rraw;
extern SrsPps *_srs_pps_objs_rfua;
extern SrsPps *_srs_pps_objs_rbuf;
extern SrsPps *_srs_pps_objs_msgs;
extern SrsPps *_srs_pps_objs_rothers;

SrsPps *_srs_pps_aloss2 = NULL;

#if defined(SRS_DEBUG) && defined(SRS_DEBUG_STATS)
SrsPps *_srs_pps_thread_run = NULL;
SrsPps *_srs_pps_thread_idle = NULL;
SrsPps *_srs_pps_thread_yield = NULL;
SrsPps *_srs_pps_thread_yield2 = NULL;

// Debug statistics for I/O operations
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

extern SrsPps *_srs_pps_sched_160ms;
extern SrsPps *_srs_pps_sched_s;
extern SrsPps *_srs_pps_sched_15ms;
extern SrsPps *_srs_pps_sched_20ms;
extern SrsPps *_srs_pps_sched_25ms;
extern SrsPps *_srs_pps_sched_30ms;
extern SrsPps *_srs_pps_sched_35ms;
extern SrsPps *_srs_pps_sched_40ms;
extern SrsPps *_srs_pps_sched_80ms;

extern SrsPps *_srs_pps_thread_run;
extern SrsPps *_srs_pps_thread_idle;
extern SrsPps *_srs_pps_thread_yield;
extern SrsPps *_srs_pps_thread_yield2;

// External ST statistics
extern __thread unsigned long long _st_stat_recvfrom;
extern __thread unsigned long long _st_stat_recvfrom_eagain;
extern __thread unsigned long long _st_stat_sendto;
extern __thread unsigned long long _st_stat_sendto_eagain;

extern __thread unsigned long long _st_stat_read;
extern __thread unsigned long long _st_stat_read_eagain;
extern __thread unsigned long long _st_stat_readv;
extern __thread unsigned long long _st_stat_readv_eagain;
extern __thread unsigned long long _st_stat_writev;
extern __thread unsigned long long _st_stat_writev_eagain;

extern __thread unsigned long long _st_stat_recvmsg;
extern __thread unsigned long long _st_stat_recvmsg_eagain;
extern __thread unsigned long long _st_stat_sendmsg;
extern __thread unsigned long long _st_stat_sendmsg_eagain;

extern __thread unsigned long long _st_stat_epoll;
extern __thread unsigned long long _st_stat_epoll_zero;
extern __thread unsigned long long _st_stat_epoll_shake;
extern __thread unsigned long long _st_stat_epoll_spin;

extern __thread unsigned long long _st_stat_sched_15ms;
extern __thread unsigned long long _st_stat_sched_20ms;
extern __thread unsigned long long _st_stat_sched_25ms;
extern __thread unsigned long long _st_stat_sched_30ms;
extern __thread unsigned long long _st_stat_sched_35ms;
extern __thread unsigned long long _st_stat_sched_40ms;
extern __thread unsigned long long _st_stat_sched_80ms;
extern __thread unsigned long long _st_stat_sched_160ms;
extern __thread unsigned long long _st_stat_sched_s;

extern __thread int _st_active_count;
extern __thread int _st_num_free_stacks;

extern __thread unsigned long long _st_stat_thread_run;
extern __thread unsigned long long _st_stat_thread_idle;
extern __thread unsigned long long _st_stat_thread_yield;
extern __thread unsigned long long _st_stat_thread_yield2;
#endif

extern SrsPps *_srs_pps_pli;
extern SrsPps *_srs_pps_twcc;
extern SrsPps *_srs_pps_rr;

extern SrsPps *_srs_pps_snack;
extern SrsPps *_srs_pps_snack2;
extern SrsPps *_srs_pps_sanack;
extern SrsPps *_srs_pps_svnack;

extern SrsPps *_srs_pps_rnack;
extern SrsPps *_srs_pps_rnack2;
extern SrsPps *_srs_pps_rhnack;
extern SrsPps *_srs_pps_rmnack;

extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

SrsResourceManager *_srs_conn_manager = NULL;

// External WebRTC global variables
extern SrsRtcBlackhole *_srs_blackhole;
extern SrsDtlsCertificate *_srs_rtc_dtls_certificate;

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
    // Initialize _srs_stages first as it's needed by SrsServer constructor
    _srs_stages = new SrsStageManager();
    _srs_sources = new SrsLiveSourceManager();
    _srs_circuit_breaker = new SrsCircuitBreaker();
    _srs_hooks = new SrsHttpHooks();

#ifdef SRS_SRT
    _srs_srt_sources = new SrsSrtSourceManager();
#endif

    _srs_rtc_sources = new SrsRtcSourceManager();
    _srs_blackhole = new SrsRtcBlackhole();

    // Initialize stream publish token manager
    _srs_stream_publish_tokens = new SrsStreamPublishTokenManager();

    _srs_conn_manager = new SrsResourceManager("RTC", true);
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

    _srs_reload_err = srs_success;
    _srs_reload_state = SrsReloadStateInit;
    _srs_reload_id = srs_rand_gen_str(7);

    return err;
}

ISrsSrtClientHandler::ISrsSrtClientHandler()
{
}

ISrsSrtClientHandler::~ISrsSrtClientHandler()
{
}

srs_error_t ISrsSrtClientHandler::accept_srt_client(srs_srt_t srt_fd)
{
    return srs_success;
}

SrsSignalManager *SrsSignalManager::instance = NULL;

SrsSignalManager::SrsSignalManager(SrsServer *s)
{
    SrsSignalManager::instance = this;

    server = s;
    sig_pipe[0] = sig_pipe[1] = -1;
    trd = new SrsSTCoroutine("signal", this, _srs_context->get_id());
    signal_read_stfd = NULL;
}

SrsSignalManager::~SrsSignalManager()
{
    srs_freep(trd);

    srs_close_stfd(signal_read_stfd);

    if (sig_pipe[0] > 0) {
        ::close(sig_pipe[0]);
    }
    if (sig_pipe[1] > 0) {
        ::close(sig_pipe[1]);
    }
}

srs_error_t SrsSignalManager::initialize()
{
    /* Create signal pipe */
    if (pipe(sig_pipe) < 0) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "create pipe");
    }

    if ((signal_read_stfd = srs_netfd_open(sig_pipe[0])) == NULL) {
        return srs_error_new(ERROR_SYSTEM_CREATE_PIPE, "open pipe");
    }

    return srs_success;
}

srs_error_t SrsSignalManager::start()
{
    srs_error_t err = srs_success;

    /**
     * Note that if multiple processes are used (see below),
     * the signal pipe should be initialized after the fork(2) call
     * so that each process has its own private pipe.
     */
    struct sigaction sa;

    /* Install sig_catcher() as a signal handler */
    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_RELOAD, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_FAST_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_GRACEFULLY_QUIT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_ASSERT_ABORT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    sa.sa_handler = SrsSignalManager::sig_catcher;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SRS_SIGNAL_REOPEN_LOG, &sa, NULL);

    srs_trace("signal installed, reload=%d, reopen=%d, fast_quit=%d, grace_quit=%d",
              SRS_SIGNAL_RELOAD, SRS_SIGNAL_REOPEN_LOG, SRS_SIGNAL_FAST_QUIT, SRS_SIGNAL_GRACEFULLY_QUIT);

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager");
    }

    return err;
}

srs_error_t SrsSignalManager::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd->pull()) != srs_success) {
            return srs_error_wrap(err, "signal manager");
        }

        int signo;

        /* Read the next signal from the pipe */
        srs_read(signal_read_stfd, &signo, sizeof(int), SRS_UTIME_NO_TIMEOUT);

        /* Process signal synchronously */
        server->on_signal(signo);
    }

    return err;
}

void SrsSignalManager::sig_catcher(int signo)
{
    int err;

    /* Save errno to restore it after the write() */
    err = errno;

    /* write() is reentrant/async-safe */
    int fd = SrsSignalManager::instance->sig_pipe[1];
    write(fd, &signo, sizeof(int));

    errno = err;
}

// Whether we are in docker, defined in main module.
extern bool _srs_in_docker;

SrsInotifyWorker::SrsInotifyWorker(SrsServer *s)
{
    server = s;
    trd = new SrsSTCoroutine("inotify", this);
    inotify_fd = NULL;
}

SrsInotifyWorker::~SrsInotifyWorker()
{
    srs_freep(trd);
    srs_close_stfd(inotify_fd);
}

srs_error_t SrsInotifyWorker::start()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    // Whether enable auto reload config.
    bool auto_reload = _srs_config->inotify_auto_reload();
    if (!auto_reload && _srs_in_docker && _srs_config->auto_reload_for_docker()) {
        srs_warn("enable auto reload for docker");
        auto_reload = true;
    }

    if (!auto_reload) {
        return err;
    }

    // Create inotify to watch config file.
    int fd = ::inotify_init1(IN_NONBLOCK);
    if (fd < 0) {
        return srs_error_new(ERROR_INOTIFY_CREATE, "create inotify");
    }

    // Open as stfd to read by ST.
    if ((inotify_fd = srs_netfd_open(fd)) == NULL) {
        ::close(fd);
        return srs_error_new(ERROR_INOTIFY_OPENFD, "open fd=%d", fd);
    }

    if (((err = srs_fd_closeexec(fd))) != srs_success) {
        return srs_error_wrap(err, "closeexec fd=%d", fd);
    }

    // /* the following are legal, implemented events that user-space can watch for */
    // #define IN_ACCESS               0x00000001      /* File was accessed */
    // #define IN_MODIFY               0x00000002      /* File was modified */
    // #define IN_ATTRIB               0x00000004      /* Metadata changed */
    // #define IN_CLOSE_WRITE          0x00000008      /* Writtable file was closed */
    // #define IN_CLOSE_NOWRITE        0x00000010      /* Unwrittable file closed */
    // #define IN_OPEN                 0x00000020      /* File was opened */
    // #define IN_MOVED_FROM           0x00000040      /* File was moved from X */
    // #define IN_MOVED_TO             0x00000080      /* File was moved to Y */
    // #define IN_CREATE               0x00000100      /* Subfile was created */
    // #define IN_DELETE               0x00000200      /* Subfile was deleted */
    // #define IN_DELETE_SELF          0x00000400      /* Self was deleted */
    // #define IN_MOVE_SELF            0x00000800      /* Self was moved */
    //
    // /* the following are legal events.  they are sent as needed to any watch */
    // #define IN_UNMOUNT              0x00002000      /* Backing fs was unmounted */
    // #define IN_Q_OVERFLOW           0x00004000      /* Event queued overflowed */
    // #define IN_IGNORED              0x00008000      /* File was ignored */
    //
    // /* helper events */
    // #define IN_CLOSE                (IN_CLOSE_WRITE | IN_CLOSE_NOWRITE) /* close */
    // #define IN_MOVE                 (IN_MOVED_FROM | IN_MOVED_TO) /* moves */
    //
    // /* special flags */
    // #define IN_ONLYDIR              0x01000000      /* only watch the path if it is a directory */
    // #define IN_DONT_FOLLOW          0x02000000      /* don't follow a sym link */
    // #define IN_EXCL_UNLINK          0x04000000      /* exclude events on unlinked objects */
    // #define IN_MASK_ADD             0x20000000      /* add to the mask of an already existing watch */
    // #define IN_ISDIR                0x40000000      /* event occurred against dir */
    // #define IN_ONESHOT              0x80000000      /* only send event once */

    // Watch the config directory events.
    string config_dir = srs_path_filepath_dir(_srs_config->config());
    uint32_t mask = IN_MODIFY | IN_CREATE | IN_MOVED_TO;
    int watch_conf = 0;
    if ((watch_conf = ::inotify_add_watch(fd, config_dir.c_str(), mask)) < 0) {
        return srs_error_new(ERROR_INOTIFY_WATCH, "watch file=%s, fd=%d, watch=%d, mask=%#x",
                             config_dir.c_str(), fd, watch_conf, mask);
    }
    srs_trace("auto reload watching fd=%d, watch=%d, file=%s", fd, watch_conf, config_dir.c_str());

    if ((err = trd->start()) != srs_success) {
        return srs_error_wrap(err, "inotify");
    }
#endif

    return err;
}

srs_error_t SrsInotifyWorker::cycle()
{
    srs_error_t err = srs_success;

#if !defined(SRS_OSX) && !defined(SRS_CYGWIN64)
    string config_path = _srs_config->config();
    string config_file = srs_path_filepath_base(config_path);
    string k8s_file = "..data";

    while (true) {
        char buf[4096];
        ssize_t nn = srs_read(inotify_fd, buf, (size_t)sizeof(buf), SRS_UTIME_NO_TIMEOUT);
        if (nn < 0) {
            srs_warn("inotify ignore read failed, nn=%d", (int)nn);
            break;
        }

        // Whether config file changed.
        bool do_reload = false;

        // Parse all inotify events.
        inotify_event *ie = NULL;
        for (char *ptr = buf; ptr < buf + nn; ptr += sizeof(inotify_event) + ie->len) {
            ie = (inotify_event *)ptr;

            if (!ie->len || !ie->name) {
                continue;
            }

            string name = ie->name;
            if ((name == k8s_file || name == config_file) && ie->mask & (IN_MODIFY | IN_CREATE | IN_MOVED_TO)) {
                do_reload = true;
            }

            srs_trace("inotify event wd=%d, mask=%#x, len=%d, name=%s, reload=%d", ie->wd, ie->mask, ie->len, ie->name, do_reload);
        }

        // Notify server to do reload.
        if (do_reload && srs_path_exists(config_path)) {
            server->on_signal(SRS_SIGNAL_RELOAD);
        }

        srs_usleep(3000 * SRS_UTIME_MILLISECONDS);
    }
#endif

    return err;
}

SrsServer::SrsServer()
{
    signal_reload_ = false;
    signal_persistence_config_ = false;
    signal_gmc_stop_ = false;
    signal_fast_quit_ = false;
    signal_gracefully_quit_ = false;
    pid_fd_ = -1;

    signal_manager_ = new SrsSignalManager(this);
    latest_version_ = new SrsLatestVersion();
    ppid_ = ::getppid();

    http_api_mux_ = new SrsHttpServeMux();

    rtmp_listener_ = new SrsMultipleTcpListeners(this);
    rtmps_listener_ = new SrsMultipleTcpListeners(this);
    api_listener_ = new SrsMultipleTcpListeners(this);
    apis_listener_ = new SrsMultipleTcpListeners(this);
    http_listener_ = new SrsMultipleTcpListeners(this);
    https_listener_ = new SrsMultipleTcpListeners(this);
    webrtc_listener_ = new SrsMultipleTcpListeners(this);
#ifdef SRS_RTSP
    rtsp_listener_ = new SrsMultipleTcpListeners(this);
#endif
    stream_caster_flv_listener_ = new SrsHttpFlvListener();
    stream_caster_mpegts_ = new SrsUdpCasterListener();
    exporter_listener_ = new SrsTcpListener(this);
#ifdef SRS_GB28181
    stream_caster_gb28181_ = new SrsGbListener(http_api_mux_);
#endif

    http_server_ = new SrsHttpServer(this);
    reuse_api_over_server_ = false;
    reuse_rtc_over_server_ = false;

    http_heartbeat_ = new SrsHttpHeartbeat();
    ingester_ = new SrsIngester();
    timer_ = NULL;

    // Initialize global shared timers moved from SrsHybridServer
    timer20ms_ = new SrsFastTimer("server", 20 * SRS_UTIME_MILLISECONDS);
    timer100ms_ = new SrsFastTimer("server", 100 * SRS_UTIME_MILLISECONDS);
    timer1s_ = new SrsFastTimer("server", 1 * SRS_UTIME_SECONDS);
    timer5s_ = new SrsFastTimer("server", 5 * SRS_UTIME_SECONDS);
    clock_monitor_ = new SrsClockWallMonitor();

    // Initialize WebRTC components
    rtc_async_ = new SrsAsyncCallWorker();
}

SrsServer::~SrsServer()
{
    destroy();
}

void SrsServer::destroy()
{
    srs_freep(timer_);

    // Free global shared timers
    srs_freep(timer20ms_);
    srs_freep(timer100ms_);
    srs_freep(timer1s_);
    srs_freep(timer5s_);
    srs_freep(clock_monitor_);

    dispose();

    // If api reuse the same port of server, they're the same object.
    if (!reuse_api_over_server_) {
        srs_freep(http_api_mux_);
    }
    srs_freep(http_server_);

    srs_freep(http_heartbeat_);
    srs_freep(ingester_);

    if (pid_fd_ > 0) {
        ::close(pid_fd_);
        pid_fd_ = -1;
    }

    srs_freep(signal_manager_);
    srs_freep(latest_version_);
    srs_freep(rtmp_listener_);
    srs_freep(rtmps_listener_);
    srs_freep(api_listener_);
    srs_freep(apis_listener_);
    srs_freep(http_listener_);
    srs_freep(https_listener_);
    srs_freep(webrtc_listener_);
#ifdef SRS_RTSP
    srs_freep(rtsp_listener_);
#endif
    srs_freep(stream_caster_flv_listener_);
    srs_freep(stream_caster_mpegts_);
    srs_freep(exporter_listener_);
#ifdef SRS_GB28181
    srs_freep(stream_caster_gb28181_);
#endif
#ifdef SRS_SRT
    close_srt_listeners();
#endif

    // Cleanup WebRTC components
    if (true) {
        std::vector<SrsUdpMuxListener *>::iterator it;
        for (it = rtc_listeners_.begin(); it != rtc_listeners_.end(); ++it) {
            SrsUdpMuxListener *listener = *it;
            srs_freep(listener);
        }
        rtc_listeners_.clear();
    }

    if (rtc_async_) {
        rtc_async_->stop();
        srs_freep(rtc_async_);
    }
}

void SrsServer::dispose()
{
    _srs_config->unsubscribe(this);

    // Destroy all listeners.
    rtmp_listener_->close();
    rtmps_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
#ifdef SRS_RTSP
    rtsp_listener_->close();
#endif
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
#ifdef SRS_SRT
    close_srt_listeners();
#endif

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester_->dispose();

    // dispose the source for hls and dvr.
    _srs_sources->dispose();

    // @remark don't dispose all connections, for too slow.
}

void SrsServer::gracefully_dispose()
{
    _srs_config->unsubscribe(this);

    // Always wait for a while to start.
    srs_usleep(_srs_config->get_grace_start_wait());
    srs_trace("start wait for %dms", srsu2msi(_srs_config->get_grace_start_wait()));

    // Destroy all listeners.
    rtmp_listener_->close();
    rtmps_listener_->close();
    api_listener_->close();
    apis_listener_->close();
    http_listener_->close();
    https_listener_->close();
    webrtc_listener_->close();
#ifdef SRS_RTSP
    rtsp_listener_->close();
#endif
    stream_caster_flv_listener_->close();
    stream_caster_mpegts_->close();
    exporter_listener_->close();
#ifdef SRS_GB28181
    stream_caster_gb28181_->close();
#endif
#ifdef SRS_SRT
    close_srt_listeners();
#endif
    srs_trace("listeners closed");

    // Fast stop to notify FFMPEG to quit, wait for a while then fast kill.
    ingester_->stop();
    srs_trace("ingesters stopped");

    // Wait for connections to quit.
    // While gracefully quiting, user can requires SRS to fast quit.
    int wait_step = 1;
    while (!_srs_conn_manager->empty() && !signal_fast_quit_) {
        for (int i = 0; i < wait_step && !_srs_conn_manager->empty() && !signal_fast_quit_; i++) {
            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
        }

        wait_step = (wait_step * 2) % 33;
        srs_trace("wait for %d conns to quit", (int)_srs_conn_manager->size());
    }

    // dispose the source for hls and dvr.
    _srs_sources->dispose();
    srs_trace("source disposed");

    srs_usleep(_srs_config->get_grace_final_wait());
    srs_trace("final wait for %dms", srsu2msi(_srs_config->get_grace_final_wait()));
}

srs_error_t SrsServer::initialize()
{
    srs_error_t err = srs_success;

    srs_trace("SRS server initialized in single thread mode");

    // Initialize the server.
    if ((err = acquire_pid_file()) != srs_success) {
        return srs_error_wrap(err, "init server");
    }

#ifdef SRS_SRT
    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

    _srt_eventloop = new SrsSrtEventLoop();

    if ((err = _srt_eventloop->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }

    if ((err = _srt_eventloop->start()) != srs_success) {
        return srs_error_wrap(err, "srt poller start");
    }
#endif

    // Initialize WebRTC DTLS certificate
    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    // Start the DVR async call.
    if ((err = _srs_dvr_async->start()) != srs_success) {
        return srs_error_wrap(err, "dvr async");
    }

    // for the main objects(server, config, log, context),
    // never subscribe handler in constructor,
    // instead, subscribe handler in initialize method.
    srs_assert(_srs_config);
    _srs_config->subscribe(this);

    bool stream = _srs_config->get_http_stream_enabled();
    vector<string> http_listens = _srs_config->get_http_stream_listens();
    vector<string> https_listens = _srs_config->get_https_stream_listens();

    bool rtc = _srs_config->get_rtc_server_enabled();
    bool rtc_tcp = _srs_config->get_rtc_server_tcp_enabled();
    vector<string> rtc_listens = _srs_config->get_rtc_server_tcp_listens();
    // If enabled and listen is the same value, resue port for WebRTC over TCP.
    if (stream && rtc && rtc_tcp && srs_strings_equal(http_listens, rtc_listens)) {
        srs_trace("WebRTC tcp=%s reuses http=%s server", srs_strings_join(rtc_listens, ",").c_str(), srs_strings_join(http_listens, ",").c_str());
        reuse_rtc_over_server_ = true;
    }
    if (stream && rtc && rtc_tcp && srs_strings_equal(https_listens, rtc_listens)) {
        srs_trace("WebRTC tcp=%s reuses https=%s server", srs_strings_join(rtc_listens, ",").c_str(), srs_strings_join(https_listens, ",").c_str());
        reuse_rtc_over_server_ = true;
    }

    // If enabled and the listen is the same value, reuse port.
    bool api = _srs_config->get_http_api_enabled();
    vector<string> api_listens = _srs_config->get_http_api_listens();
    vector<string> apis_listens = _srs_config->get_https_api_listens();
    if (stream && api && srs_strings_equal(api_listens, http_listens) && srs_strings_equal(apis_listens, https_listens)) {
        srs_trace("API reuses http=%s and https=%s server", srs_strings_join(http_listens, ",").c_str(), srs_strings_join(https_listens, ",").c_str());
        reuse_api_over_server_ = true;
    }

    // Only init HTTP API when not reusing HTTP server.
    if (!reuse_api_over_server_) {
        SrsHttpServeMux *api = dynamic_cast<SrsHttpServeMux *>(http_api_mux_);
        srs_assert(api);

        if ((err = api->initialize()) != srs_success) {
            return srs_error_wrap(err, "http api initialize");
        }
    } else {
        srs_freep(http_api_mux_);
        http_api_mux_ = http_server_;
    }

    if ((err = http_server_->initialize()) != srs_success) {
        return srs_error_wrap(err, "http server initialize");
    }

    // Initialize the black hole.
    if ((err = _srs_blackhole->initialize()) != srs_success) {
        return srs_error_wrap(err, "black hole");
    }

    // Start WebRTC async worker
    rtc_async_->start();

    // Start global shared timers
    if ((err = timer20ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer20ms");
    }

    if ((err = timer100ms_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer100ms");
    }

    if ((err = timer1s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer1s");
    }

    if ((err = timer5s_->start()) != srs_success) {
        return srs_error_wrap(err, "start timer5s");
    }

    // Register clock monitor to 20ms timer and statistics reporting to 5s timer
    timer20ms_->subscribe(clock_monitor_);
    timer5s_->subscribe(this);

    return err;
}

srs_error_t SrsServer::acquire_pid_file()
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
    string pid = srs_strconv_format_int(getpid());
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
    pid_fd_ = fd;

    return srs_success;
}

srs_error_t SrsServer::run()
{
    srs_error_t err = srs_success;

    // Circuit breaker to protect server, which depends on server.
    if ((err = _srs_circuit_breaker->initialize()) != srs_success) {
        return srs_error_wrap(err, "init circuit breaker");
    }

    // Initialize the whole system, set hooks to handle server level events.
    if ((err = initialize_st()) != srs_success) {
        return srs_error_wrap(err, "initialize st");
    }

    if ((err = initialize_signal()) != srs_success) {
        return srs_error_wrap(err, "initialize signal");
    }

    if ((err = listen()) != srs_success) {
        return srs_error_wrap(err, "listen");
    }

    if ((err = register_signal()) != srs_success) {
        return srs_error_wrap(err, "register signal");
    }

    if ((err = http_handle()) != srs_success) {
        return srs_error_wrap(err, "http handle");
    }

    if ((err = ingest()) != srs_success) {
        return srs_error_wrap(err, "ingest");
    }

    if ((err = _srs_sources->initialize()) != srs_success) {
        return srs_error_wrap(err, "live sources");
    }

#ifdef SRS_SRT
    if ((err = _srs_srt_sources->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt sources");
    }
#endif

    if ((err = _srs_rtc_sources->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc sources");
    }

#ifdef SRS_RTSP
    if ((err = _srs_rtsp_sources->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtsp sources");
    }
#endif

    if ((err = setup_ticks()) != srs_success) {
        return srs_error_wrap(err, "tick");
    }

#ifdef SRS_GB28181
    if ((err = _srs_gb_manager->start()) != srs_success) {
        return srs_error_wrap(err, "start manager");
    }
#endif

    return cycle();
}

srs_error_t SrsServer::initialize_st()
{
    srs_error_t err = srs_success;

    // check asprocess.
    bool asprocess = _srs_config->get_asprocess();
    if (asprocess && ppid_ == 1) {
        return srs_error_new(ERROR_SYSTEM_ASSERT_FAILED, "ppid=%d illegal for asprocess", ppid_);
    }

    srs_trace("server main cid=%s, pid=%d, ppid=%d, asprocess=%d",
              _srs_context->get_id().c_str(), ::getpid(), ppid_, asprocess);

    return err;
}

srs_error_t SrsServer::initialize_signal()
{
    srs_error_t err = srs_success;

    if ((err = signal_manager_->initialize()) != srs_success) {
        return srs_error_wrap(err, "init signal manager");
    }

    // Start the version query coroutine.
    if ((err = latest_version_->start()) != srs_success) {
        return srs_error_wrap(err, "start version query");
    }

    return err;
}

srs_error_t SrsServer::listen()
{
    srs_error_t err = srs_success;

    // Create RTMP listeners.
    rtmp_listener_->add(_srs_config->get_listens())->set_label("RTMP");
    if ((err = rtmp_listener_->listen()) != srs_success) {
        return srs_error_wrap(err, "rtmp listen");
    }

    // Create RTMPS listeners.
    if (_srs_config->get_rtmps_enabled()) {
        rtmps_listener_->add(_srs_config->get_rtmps_listen())->set_label("RTMPS");
        if ((err = rtmps_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "rtmps listen");
        }
    }

    // Create HTTP API listener.
    if (_srs_config->get_http_api_enabled()) {
        if (reuse_api_over_server_) {
            vector<string> listens = _srs_config->get_http_stream_listens();
            srs_trace("HTTP-API: Reuse listen to http server %s", srs_strings_join(listens, ",").c_str());
        } else {
            api_listener_->add(_srs_config->get_http_api_listens())->set_label("HTTP-API");
            if ((err = api_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "http api listen");
            }
        }
    }

    // Create HTTPS API listener.
    if (_srs_config->get_https_api_enabled()) {
        if (reuse_api_over_server_) {
            vector<string> listens = _srs_config->get_https_stream_listens();
            srs_trace("HTTPS-API: Reuse listen to http server %s", srs_strings_join(listens, ",").c_str());
        } else {
            apis_listener_->add(_srs_config->get_https_api_listens())->set_label("HTTPS-API");
            if ((err = apis_listener_->listen()) != srs_success) {
                return srs_error_wrap(err, "https api listen");
            }
        }
    }

    // Create HTTP server listener.
    if (_srs_config->get_http_stream_enabled()) {
        http_listener_->add(_srs_config->get_http_stream_listens())->set_label("HTTP-Server");
        if ((err = http_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "http server listen");
        }
    }

    // Create HTTPS server listener.
    if (_srs_config->get_https_stream_enabled()) {
        https_listener_->add(_srs_config->get_https_stream_listens())->set_label("HTTPS-Server");
        if ((err = https_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "https server listen");
        }
    }

    // Start WebRTC over TCP listener.
    string protocol = _srs_config->get_rtc_server_protocol();
    if (!reuse_rtc_over_server_ && protocol != "udp" && _srs_config->get_rtc_server_tcp_enabled()) {
        webrtc_listener_->add(_srs_config->get_rtc_server_tcp_listens())->set_label("WebRTC");
        if ((err = webrtc_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "webrtc tcp listen");
        }
    }

#ifdef SRS_RTSP
    // Start RTSP listener. RTC is a critical dependency.
    if (_srs_config->get_rtsp_server_enabled()) {
        rtsp_listener_->add(_srs_config->get_rtsp_server_listens())->set_label("RTSP");
        if ((err = rtsp_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "rtsp listen");
        }
    }
#endif

    // Start all listeners for stream caster.
    std::vector<SrsConfDirective *> confs = _srs_config->get_stream_casters();
    for (vector<SrsConfDirective *>::iterator it = confs.begin(); it != confs.end(); ++it) {
        SrsConfDirective *conf = *it;
        if (!_srs_config->get_stream_caster_enabled(conf)) {
            continue;
        }

        ISrsListener *listener = NULL;
        std::string caster = _srs_config->get_stream_caster_engine(conf);
        if (srs_stream_caster_is_udp(caster)) {
            listener = stream_caster_mpegts_;
            if ((err = stream_caster_mpegts_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_flv(caster)) {
            listener = stream_caster_flv_listener_;
            if ((err = stream_caster_flv_listener_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
        } else if (srs_stream_caster_is_gb28181(caster)) {
#ifdef SRS_GB28181
            listener = stream_caster_gb28181_;
            if ((err = stream_caster_gb28181_->initialize(conf)) != srs_success) {
                return srs_error_wrap(err, "initialize");
            }
#else
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "Please enable GB by: ./configure --gb28181=on");
#endif
        } else {
            return srs_error_new(ERROR_STREAM_CASTER_ENGINE, "invalid caster %s", caster.c_str());
        }

        srs_assert(listener);
        if ((err = listener->listen()) != srs_success) {
            return srs_error_wrap(err, "listen");
        }
    }

    // Create exporter server listener.
    if (_srs_config->get_exporter_enabled()) {
        exporter_listener_->set_endpoint(_srs_config->get_exporter_listen())->set_label("Exporter-Server");
        if ((err = exporter_listener_->listen()) != srs_success) {
            return srs_error_wrap(err, "exporter server listen");
        }
    }

#ifdef SRS_SRT
    // Listen MPEG-TS over SRT.
    if ((err = listen_srt_mpegts()) != srs_success) {
        return srs_error_wrap(err, "srt mpegts listen");
    }
#endif

    // Listen WebRTC UDP.
    if ((err = listen_rtc_udp()) != srs_success) {
        return srs_error_wrap(err, "rtc udp listen");
    }

    if ((err = _srs_conn_manager->start()) != srs_success) {
        return srs_error_wrap(err, "connection manager");
    }

    return err;
}

srs_error_t SrsServer::register_signal()
{
    srs_error_t err = srs_success;

    if ((err = signal_manager_->start()) != srs_success) {
        return srs_error_wrap(err, "signal manager start");
    }

    return err;
}

srs_error_t SrsServer::http_handle()
{
    srs_error_t err = srs_success;

    // Ignore / and /api/v1/versions for already handled by HTTP server.
    if (!reuse_api_over_server_) {
        if ((err = http_api_mux_->handle("/", new SrsGoApiRoot())) != srs_success) {
            return srs_error_wrap(err, "handle /");
        }
        if ((err = http_api_mux_->handle("/api/v1/versions", new SrsGoApiVersion())) != srs_success) {
            return srs_error_wrap(err, "handle versions");
        }
    }

    if ((err = http_api_mux_->handle("/api/", new SrsGoApiApi())) != srs_success) {
        return srs_error_wrap(err, "handle api");
    }
    if ((err = http_api_mux_->handle("/api/v1/", new SrsGoApiV1())) != srs_success) {
        return srs_error_wrap(err, "handle v1");
    }
    if ((err = http_api_mux_->handle("/api/v1/summaries", new SrsGoApiSummaries())) != srs_success) {
        return srs_error_wrap(err, "handle summaries");
    }
    if ((err = http_api_mux_->handle("/api/v1/rusages", new SrsGoApiRusages())) != srs_success) {
        return srs_error_wrap(err, "handle rusages");
    }
    if ((err = http_api_mux_->handle("/api/v1/self_proc_stats", new SrsGoApiSelfProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle self proc stats");
    }
    if ((err = http_api_mux_->handle("/api/v1/system_proc_stats", new SrsGoApiSystemProcStats())) != srs_success) {
        return srs_error_wrap(err, "handle system proc stats");
    }
    if ((err = http_api_mux_->handle("/api/v1/meminfos", new SrsGoApiMemInfos())) != srs_success) {
        return srs_error_wrap(err, "handle meminfos");
    }
    if ((err = http_api_mux_->handle("/api/v1/authors", new SrsGoApiAuthors())) != srs_success) {
        return srs_error_wrap(err, "handle authors");
    }
    if ((err = http_api_mux_->handle("/api/v1/features", new SrsGoApiFeatures())) != srs_success) {
        return srs_error_wrap(err, "handle features");
    }
    if ((err = http_api_mux_->handle("/api/v1/vhosts/", new SrsGoApiVhosts())) != srs_success) {
        return srs_error_wrap(err, "handle vhosts");
    }
    if ((err = http_api_mux_->handle("/api/v1/streams/", new SrsGoApiStreams())) != srs_success) {
        return srs_error_wrap(err, "handle streams");
    }
    if ((err = http_api_mux_->handle("/api/v1/clients/", new SrsGoApiClients())) != srs_success) {
        return srs_error_wrap(err, "handle clients");
    }
    if ((err = http_api_mux_->handle("/api/v1/raw", new SrsGoApiRaw(this))) != srs_success) {
        return srs_error_wrap(err, "handle raw");
    }
    if ((err = http_api_mux_->handle("/api/v1/clusters", new SrsGoApiClusters())) != srs_success) {
        return srs_error_wrap(err, "handle clusters");
    }

    // test the request info.
    if ((err = http_api_mux_->handle("/api/v1/tests/requests", new SrsGoApiRequests())) != srs_success) {
        return srs_error_wrap(err, "handle tests requests");
    }
    // test the error code response.
    if ((err = http_api_mux_->handle("/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }
    // test the redirect mechenism.
    if ((err = http_api_mux_->handle("/api/v1/tests/redirects", new SrsHttpRedirectHandler("/api/v1/tests/errors", SRS_CONSTS_HTTP_MovedPermanently))) != srs_success) {
        return srs_error_wrap(err, "handle tests redirects");
    }
    // test the http vhost.
    if ((err = http_api_mux_->handle("error.srs.com/api/v1/tests/errors", new SrsGoApiError())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors for error.srs.com");
    }

#ifdef SRS_GPERF
    // The test api for get tcmalloc stats.
    // @see Memory Introspection in https://gperftools.github.io/gperftools/tcmalloc.html
    if ((err = http_api_mux_->handle("/api/v1/tcmalloc", new SrsGoApiTcmalloc())) != srs_success) {
        return srs_error_wrap(err, "handle tcmalloc errors");
    }
#endif

#ifdef SRS_VALGRIND
    // The test api for valgrind. See VALGRIND_DO_LEAK_CHECK in https://valgrind.org/docs/manual/mc-manual.html
    if ((err = http_api_mux_->handle("/api/v1/valgrind", new SrsGoApiValgrind())) != srs_success) {
        return srs_error_wrap(err, "handle valgrind errors");
    }
#endif

#ifdef SRS_SIGNAL_API
    // Simulate the signal by HTTP API, for debug signal issues in CLion.
    if ((err = http_api_mux_->handle("/api/v1/signal", new SrsGoApiSignal())) != srs_success) {
        return srs_error_wrap(err, "handle signal errors");
    }
#endif

    // metrics by prometheus
    if ((err = http_api_mux_->handle("/metrics", new SrsGoApiMetrics())) != srs_success) {
        return srs_error_wrap(err, "handle tests errors");
    }

    // TODO: FIXME: for console.
    // TODO: FIXME: support reload.
    std::string dir = _srs_config->get_http_stream_dir() + "/console";
    if ((err = http_api_mux_->handle("/console/", new SrsHttpFileServer(dir))) != srs_success) {
        return srs_error_wrap(err, "handle console at %s", dir.c_str());
    }
    srs_trace("http: api mount /console to %s", dir.c_str());

    // WebRTC API endpoints
    if ((err = listen_rtc_api()) != srs_success) {
        return srs_error_wrap(err, "rtc api");
    }

    return err;
}

srs_error_t SrsServer::ingest()
{
    srs_error_t err = srs_success;

    if ((err = ingester_->start()) != srs_success) {
        return srs_error_wrap(err, "ingest start");
    }

    return err;
}

void SrsServer::stop()
{
#ifdef SRS_GPERF_MC
    dispose();

    // remark, for gmc, never invoke the exit().
    srs_warn("sleep a long time for system st-threads to cleanup.");
    srs_usleep(3 * 1000 * 1000);
    srs_warn("system quit");

    // For GCM, cleanup done.
    return;
#endif

    // quit normally.
    srs_warn("main cycle terminated, system quit normally.");

    // fast quit, do some essential cleanup.
    if (signal_fast_quit_) {
        dispose(); // TODO: FIXME: Rename to essential_dispose.
        srs_trace("srs disposed");
    }

    // gracefully quit, do carefully cleanup.
    if (signal_gracefully_quit_) {
        gracefully_dispose();
        srs_trace("srs gracefully quit");
    }

    // This is the last line log of SRS.
    srs_trace("srs terminated");
}

srs_error_t SrsServer::cycle()
{
    srs_error_t err = srs_success;

    // Start the inotify auto reload by watching config file.
    SrsInotifyWorker inotify(this);
    if ((err = inotify.start()) != srs_success) {
        return srs_error_wrap(err, "start inotify");
    }

    // Do server main cycle.
    if ((err = do_cycle()) != srs_success) {
        srs_error("server err %s", srs_error_desc(err).c_str());
    }

    return err;
}

void SrsServer::on_signal(int signo)
{
    // For signal to quit with coredump.
    if (signo == SRS_SIGNAL_ASSERT_ABORT) {
        srs_trace("abort with coredump, signo=%d", signo);
        srs_assert(false);
        return;
    }

    if (signo == SRS_SIGNAL_RELOAD) {
        srs_trace("reload config, signo=%d", signo);
        signal_reload_ = true;
        return;
    }

#ifndef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        _srs_log->reopen();

        srs_warn("reopen log file, signo=%d", signo);
        return;
    }
#endif

#ifdef SRS_GPERF_MC
    if (signo == SRS_SIGNAL_REOPEN_LOG) {
        signal_gmc_stop_ = true;
        srs_warn("for gmc, the SIGUSR1 used as SIGINT, signo=%d", signo);
        return;
    }
#endif

    if (signo == SRS_SIGNAL_PERSISTENCE_CONFIG) {
        signal_persistence_config_ = true;
        return;
    }

    if (signo == SIGINT) {
#ifdef SRS_GPERF_MC
        srs_trace("gmc is on, main cycle will terminate normally, signo=%d", signo);
        signal_gmc_stop_ = true;
#endif
    }

    // For K8S, force to gracefully quit for gray release or canary.
    // @see https://github.com/ossrs/srs/issues/1595#issuecomment-587473037
    if (signo == SRS_SIGNAL_FAST_QUIT && _srs_config->is_force_grace_quit()) {
        srs_trace("force gracefully quit, signo=%d", signo);
        signo = SRS_SIGNAL_GRACEFULLY_QUIT;
    }

    if ((signo == SIGINT || signo == SRS_SIGNAL_FAST_QUIT) && !signal_fast_quit_) {
        srs_trace("sig=%d, user terminate program, fast quit", signo);
        signal_fast_quit_ = true;
        return;
    }

    if (signo == SRS_SIGNAL_GRACEFULLY_QUIT && !signal_gracefully_quit_) {
        srs_trace("sig=%d, user start gracefully quit", signo);
        signal_gracefully_quit_ = true;
        return;
    }
}

srs_error_t _srs_reload_err;
SrsReloadState _srs_reload_state;
std::string _srs_reload_id;

srs_error_t SrsServer::do_cycle()
{
    srs_error_t err = srs_success;

    // for asprocess.
    bool asprocess = _srs_config->get_asprocess();

    while (true) {
        // asprocess check.
        if (asprocess && ::getppid() != ppid_) {
            return srs_error_new(ERROR_ASPROCESS_PPID, "asprocess ppid changed from %d to %d", ppid_, ::getppid());
        }

        // gracefully quit for SIGINT or SIGTERM or SIGQUIT.
        if (signal_fast_quit_ || signal_gracefully_quit_) {
            srs_trace("cleanup for quit signal fast=%d, grace=%d", signal_fast_quit_, signal_gracefully_quit_);
            return err;
        }

        // for gperf heap checker,
        // @see: research/gperftools/heap-checker/heap_checker.cc
        // if user interrupt the program, exit to check mem leak.
        // but, if gperf, use reload to ensure main return normally,
        // because directly exit will cause core-dump.
#ifdef SRS_GPERF_MC
        if (signal_gmc_stop_) {
            srs_warn("gmc got singal to stop server.");
            return err;
        }
#endif

        // do persistence config to file.
        if (signal_persistence_config_) {
            signal_persistence_config_ = false;
            srs_info("get signal to persistence config to file.");

            if ((err = _srs_config->persistence()) != srs_success) {
                return srs_error_wrap(err, "config persistence to file");
            }
            srs_trace("persistence config to file success.");
        }

        // do reload the config.
        if (signal_reload_) {
            signal_reload_ = false;
            srs_trace("starting reload config.");

            SrsReloadState state = SrsReloadStateInit;
            _srs_reload_state = SrsReloadStateInit;
            srs_freep(_srs_reload_err);
            _srs_reload_id = srs_rand_gen_str(7);
            err = _srs_config->reload(&state);
            _srs_reload_state = state;
            _srs_reload_err = srs_error_copy(err);
            if (err != srs_success) {
                // If the parsing and transformation of the configuration fail, we can tolerate it by simply
                // ignoring the new configuration and continuing to use the current one. However, if the
                // application of the new configuration fails, some configurations may be applied while
                // others may not. For instance, the listening port may be closed when the configuration
                // is set to listen on an unavailable port. In such cases, we should terminate the service.
                if (state == SrsReloadStateApplying) {
                    return srs_error_wrap(err, "reload fatal error state=%d", state);
                }

                srs_warn("reload failed, state=%d, err %s", state, srs_error_desc(err).c_str());
                srs_freep(err);
            } else {
                srs_trace("reload config success, state=%d.", state);
            }
        }

        srs_usleep(1 * SRS_UTIME_SECONDS);
    }

    return err;
}

srs_error_t SrsServer::setup_ticks()
{
    srs_error_t err = srs_success;

    srs_freep(timer_);
    timer_ = new SrsHourGlass("srs", this, 1 * SRS_UTIME_SECONDS);

    if (_srs_config->get_stats_enabled()) {
        if ((err = timer_->tick(2, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(4, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(5, 6 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(6, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
        if ((err = timer_->tick(7, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(8, 3 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(10, 9 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }

        if ((err = timer_->tick(11, 5 * SRS_UTIME_SECONDS)) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if (_srs_config->get_heartbeat_enabled()) {
        if ((err = timer_->tick(9, _srs_config->get_heartbeat_interval())) != srs_success) {
            return srs_error_wrap(err, "tick");
        }
    }

    if ((err = timer_->start()) != srs_success) {
        return srs_error_wrap(err, "timer");
    }

    return err;
}

srs_error_t SrsServer::notify(int event, srs_utime_t interval, srs_utime_t tick)
{
    srs_error_t err = srs_success;

    switch (event) {
    case 2:
        srs_update_system_rusage();
        break;
    case 4:
        srs_update_disk_stat();
        break;
    case 5:
        srs_update_meminfo();
        break;
    case 6:
        srs_update_platform_info();
        break;
    case 7:
        srs_update_network_devices();
        break;
    case 8:
        resample_kbps();
        break;
    case 9:
        http_heartbeat_->heartbeat();
        break;
    case 10:
        srs_update_udp_snmp_statistic();
        break;
    case 11:
        srs_update_rtc_sessions();
        break;
    }

    return err;
}

void SrsServer::resample_kbps()
{
    SrsStatistic *stat = SrsStatistic::instance();

    // collect delta from all clients.
    for (int i = 0; i < (int)_srs_conn_manager->size(); i++) {
        ISrsResource *c = _srs_conn_manager->at(i);

        SrsRtmpConn *rtmp = dynamic_cast<SrsRtmpConn *>(c);
        if (rtmp) {
            stat->kbps_add_delta(c->get_id().c_str(), rtmp->delta());
            continue;
        }

        SrsHttpxConn *httpx = dynamic_cast<SrsHttpxConn *>(c);
        if (httpx) {
            stat->kbps_add_delta(c->get_id().c_str(), httpx->delta());
            continue;
        }

#ifdef SRS_RTSP
        SrsRtspConnection *rtsp = dynamic_cast<SrsRtspConnection *>(c);
        if (rtsp) {
            stat->kbps_add_delta(c->get_id().c_str(), rtsp->delta());
            continue;
        }
#endif

        SrsRtcTcpConn *tcp = dynamic_cast<SrsRtcTcpConn *>(c);
        if (tcp) {
            stat->kbps_add_delta(c->get_id().c_str(), tcp->delta());
            continue;
        }

#ifdef SRS_SRT
        SrsMpegtsSrtConn *srt = dynamic_cast<SrsMpegtsSrtConn *>(c);
        if (srt) {
            stat->kbps_add_delta(c->get_id().c_str(), srt->delta());
            continue;
        }
#endif

        SrsRtcConnection *rtc = dynamic_cast<SrsRtcConnection *>(c);
        if (rtc) {
            stat->kbps_add_delta(c->get_id().c_str(), rtc->delta());
            continue;
        }

        // Impossible path, because we only create these connections above.
        srs_assert(false);
    }

    // Update the global server level statistics.
    stat->kbps_sample();
}

#ifdef SRS_SRT
srs_error_t SrsServer::listen_srt_mpegts()
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_srt_enabled()) {
        return err;
    }

    // Close all listener for SRT if exists.
    close_srt_listeners();

    // Start listeners for SRT, support multiple addresses including IPv6.
    vector<string> srt_listens = _srs_config->get_srt_listens();
    for (int i = 0; i < (int)srt_listens.size(); i++) {
        SrsSrtAcceptor *acceptor = new SrsSrtAcceptor(this);

        int port;
        string ip;
        srs_net_split_for_listener(srt_listens[i], ip, port);

        if ((err = acceptor->listen(ip, port)) != srs_success) {
            srs_freep(acceptor);
            srs_warn("srt listen %s:%d failed, err=%s", ip.c_str(), port, srs_error_desc(err).c_str());
            srs_error_reset(err);
            continue;
        }

        srt_acceptors_.push_back(acceptor);
    }

    // Check if at least one listener succeeded
    if (srt_acceptors_.empty()) {
        return srs_error_new(ERROR_SOCKET_LISTEN, "no srt listeners available");
    }

    return err;
}

void SrsServer::close_srt_listeners()
{
    std::vector<SrsSrtAcceptor *>::iterator it;
    for (it = srt_acceptors_.begin(); it != srt_acceptors_.end();) {
        SrsSrtAcceptor *acceptor = *it;
        srs_freep(acceptor);

        it = srt_acceptors_.erase(it);
    }
}

srs_error_t SrsServer::accept_srt_client(srs_srt_t srt_fd)
{
    srs_error_t err = srs_success;

    ISrsResource *resource = NULL;
    if ((err = srt_fd_to_resource(srt_fd, &resource)) != srs_success) {
        // close fd on conn error, otherwise will lead to fd leak
        srs_srt_close(srt_fd);
        return srs_error_wrap(err, "srt fd to resource");
    }
    srs_assert(resource);

    // directly enqueue, the cycle thread will remove the client.
    _srs_conn_manager->add(resource);

    // Note that conn is managed by _srs_conn_manager, so we don't need to free it.
    ISrsStartable *conn = dynamic_cast<ISrsStartable *>(resource);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start srt conn coroutine");
    }

    return err;
}

srs_error_t SrsServer::srt_fd_to_resource(srs_srt_t srt_fd, ISrsResource **pr)
{
    srs_error_t err = srs_success;

    string ip = "";
    int port = 0;
    if ((err = srs_srt_get_remote_ip_port(srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "get srt ip port");
    }

    // Security or system flow control check.
    if ((err = on_before_connection("SRT", (int)srt_fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // Convert to SRT connection.
    *pr = new SrsMpegtsSrtConn(_srs_conn_manager, srt_fd, ip, port);

    return err;
}
#endif

srs_error_t SrsServer::listen_rtc_udp()
{
    srs_error_t err = srs_success;

    if (!_srs_config->get_rtc_server_enabled()) {
        return err;
    }

    // Check protocol setting - only create UDP listeners if protocol allows UDP
    string protocol = _srs_config->get_rtc_server_protocol();
    if (protocol == "tcp") {
        // When protocol is "tcp", don't create UDP listeners
        return err;
    }

    vector<string> rtc_listens = _srs_config->get_rtc_server_listens();
    if (rtc_listens.empty()) {
        return srs_error_new(ERROR_RTC_PORT, "empty rtc listen");
    }

    // There should be no listeners before listening.
    srs_assert(rtc_listeners_.empty());

    // For each listen address, create multiple listeners based on reuseport setting
    int nn_listeners = _srs_config->get_rtc_server_reuseport();
    for (int j = 0; j < (int)rtc_listens.size(); j++) {
        string ip;
        int port;
        srs_net_split_for_listener(rtc_listens[j], ip, port);

        if (port <= 0) {
            return srs_error_new(ERROR_RTC_PORT, "invalid port=%d", port);
        }

        for (int i = 0; i < nn_listeners; i++) {
            SrsUdpMuxListener *listener = new SrsUdpMuxListener(this, ip, port);

            if ((err = listener->listen()) != srs_success) {
                srs_freep(listener);
                return srs_error_wrap(err, "listen %s:%d", ip.c_str(), port);
            }

            srs_trace("WebRTC listen at udp://%s:%d, fd=%d", ip.c_str(), port, listener->fd());
            rtc_listeners_.push_back(listener);
        }
    }

    return err;
}

srs_error_t SrsServer::on_udp_packet(SrsUdpMuxSocket *skt)
{
    srs_error_t err = srs_success;

    SrsRtcConnection *session = NULL;
    char *data = skt->data();
    int size = skt->size();
    bool is_rtp_or_rtcp = srs_is_rtp_or_rtcp((uint8_t *)data, size);
    bool is_rtcp = srs_is_rtcp((uint8_t *)data, size);

    uint64_t fast_id = skt->fast_id();
    // Try fast id first, if not found, search by long peer id.
    if (fast_id) {
        session = (SrsRtcConnection *)_srs_conn_manager->find_by_fast_id(fast_id);
    }
    if (!session) {
        string peer_id = skt->peer_id();
        session = (SrsRtcConnection *)_srs_conn_manager->find_by_id(peer_id);
    }

    if (session) {
        // When got any packet, the session is alive now.
        session->alive();
    }

    // For STUN, the peer address may change.
    if (!is_rtp_or_rtcp && srs_is_stun((uint8_t *)data, size)) {
        ++_srs_pps_rstuns->sugar;
        string peer_id = skt->peer_id();

        // TODO: FIXME: Should support ICE renomination, to switch network between candidates.
        SrsStunPacket ping;
        if ((err = ping.decode(data, size)) != srs_success) {
            return srs_error_wrap(err, "decode stun packet failed");
        }
        if (!session) {
            session = find_rtc_session_by_username(ping.get_username());
        }
        if (session) {
            session->switch_to_context();
        }

        srs_info("recv stun packet from %s, fast=%" PRId64 ", use-candidate=%d, ice-controlled=%d, ice-controlling=%d",
                 peer_id.c_str(), fast_id, ping.get_use_candidate(), ping.get_ice_controlled(), ping.get_ice_controlling());

        // TODO: FIXME: For ICE trickle, we may get STUN packets before SDP answer, so maybe should response it.
        if (!session) {
            return srs_error_new(ERROR_RTC_STUN, "no session, stun username=%s, peer_id=%s, fast=%" PRId64,
                                 ping.get_username().c_str(), peer_id.c_str(), fast_id);
        }

        // For each binding request, update the UDP socket.
        if (ping.is_binding_request()) {
            session->udp()->update_sendonly_socket(skt);
        }

        return session->udp()->on_stun(&ping, data, size);
    }

    // For DTLS, RTCP or RTP, which does not support peer address changing.
    if (!session) {
        string peer_id = skt->peer_id();
        return srs_error_new(ERROR_RTC_STUN, "no session, peer_id=%s, fast=%" PRId64, peer_id.c_str(), fast_id);
    }

    // Note that we don't(except error) switch to the context of session, for performance issue.
    if (is_rtp_or_rtcp && !is_rtcp) {
        ++_srs_pps_rrtps->sugar;

        err = session->udp()->on_rtp(data, size);
        if (err != srs_success) {
            session->switch_to_context();
        }
        return err;
    }

    session->switch_to_context();
    if (is_rtp_or_rtcp && is_rtcp) {
        ++_srs_pps_rrtcps->sugar;

        return session->udp()->on_rtcp(data, size);
    }
    if (srs_is_dtls((uint8_t *)data, size)) {
        ++_srs_pps_rstuns->sugar;

        return session->udp()->on_dtls(data, size);
    }
    return srs_error_new(ERROR_RTC_UDP, "unknown packet");
}

srs_error_t SrsServer::listen_rtc_api()
{
    srs_error_t err = srs_success;

    if ((err = http_api_mux_->handle("/rtc/v1/play/", new SrsGoApiRtcPlay(this))) != srs_success) {
        return srs_error_wrap(err, "handle play");
    }

    if ((err = http_api_mux_->handle("/rtc/v1/publish/", new SrsGoApiRtcPublish(this))) != srs_success) {
        return srs_error_wrap(err, "handle publish");
    }

    // Generally, WHIP is a publishing protocol, but it can be also used as playing.
    // See https://datatracker.ietf.org/doc/draft-ietf-wish-whep/
    if ((err = http_api_mux_->handle("/rtc/v1/whip/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whip");
    }

    // We create another mount, to support play with the same query string as publish.
    // See https://datatracker.ietf.org/doc/draft-murillo-whep/
    if ((err = http_api_mux_->handle("/rtc/v1/whip-play/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whep play");
    }
    if ((err = http_api_mux_->handle("/rtc/v1/whep/", new SrsGoApiRtcWhip(this))) != srs_success) {
        return srs_error_wrap(err, "handle whep play");
    }

#ifdef SRS_SIMULATOR
    if ((err = http_api_mux_->handle("/rtc/v1/nack/", new SrsGoApiRtcNACK(this))) != srs_success) {
        return srs_error_wrap(err, "handle nack");
    }
#endif

    return err;
}

srs_error_t SrsServer::exec_rtc_async_work(ISrsAsyncCallTask *t)
{
    return rtc_async_->execute(t);
}

SrsRtcConnection *SrsServer::find_rtc_session_by_username(const std::string &username)
{
    ISrsResource *conn = _srs_conn_manager->find_by_name(username);
    return dynamic_cast<SrsRtcConnection *>(conn);
}

srs_error_t SrsServer::create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, SrsRtcConnection **psession)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = ruc->req_;

    // Security or system flow control check. For WebRTC, use 0 as fd and port, because for
    // the WebRTC HTTP API, it's not useful information.
    if ((err = on_before_connection("RTC", (int)0, req->ip, 0)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // Acquire stream publish token to prevent race conditions across all protocols.
    SrsStreamPublishToken *publish_token_raw = NULL;
    if (ruc->publish_ && (err = _srs_stream_publish_tokens->acquire_token(req, publish_token_raw)) != srs_success) {
        return srs_error_wrap(err, "acquire stream publish token");
    }
    SrsUniquePtr<SrsStreamPublishToken> publish_token(publish_token_raw);
    if (publish_token.get()) {
        srs_trace("stream publish token acquired, type=rtc, url=%s", req->get_stream_url().c_str());
    }

    SrsSharedPtr<SrsRtcSource> source;
    if ((err = _srs_rtc_sources->fetch_or_create(req, source)) != srs_success) {
        return srs_error_wrap(err, "create source");
    }

    if (ruc->publish_ && !source->can_publish()) {
        return srs_error_new(ERROR_RTC_SOURCE_BUSY, "stream %s busy", req->get_stream_url().c_str());
    }

    // TODO: FIXME: add do_create_session to error process.
    SrsContextId cid = _srs_context->get_id();
    SrsRtcConnection *session = new SrsRtcConnection(this, cid);
    if ((err = do_create_rtc_session(ruc, local_sdp, session)) != srs_success) {
        srs_freep(session);
        return srs_error_wrap(err, "create session");
    }

    *psession = session;

    return err;
}

srs_error_t SrsServer::do_create_rtc_session(SrsRtcUserConfig *ruc, SrsSdp &local_sdp, SrsRtcConnection *session)
{
    srs_error_t err = srs_success;

    ISrsRequest *req = ruc->req_;

    // first add publisher/player for negotiate sdp media info
    if (ruc->publish_) {
        if ((err = session->add_publisher(ruc, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add publisher");
        }
    } else {
        if ((err = session->add_player(ruc, local_sdp)) != srs_success) {
            return srs_error_wrap(err, "add player");
        }
    }

    // All tracks default as inactive, so we must enable them.
    session->set_all_tracks_status(req->get_stream_url(), ruc->publish_, true);

    std::string local_pwd = ruc->req_->ice_pwd_.empty() ? srs_rand_gen_str(32) : ruc->req_->ice_pwd_;
    std::string local_ufrag = ruc->req_->ice_ufrag_.empty() ? srs_rand_gen_str(8) : ruc->req_->ice_ufrag_;
    // TODO: FIXME: Rename for a better name, it's not an username.
    std::string username = "";
    while (true) {
        username = local_ufrag + ":" + ruc->remote_sdp_.get_ice_ufrag();
        if (!_srs_conn_manager->find_by_name(username)) {
            break;
        }

        // Username conflict, regenerate a new one.
        local_ufrag = srs_rand_gen_str(8);
    }

    local_sdp.set_ice_ufrag(local_ufrag);
    local_sdp.set_ice_pwd(local_pwd);
    local_sdp.set_fingerprint_algo("sha-256");
    local_sdp.set_fingerprint(_srs_rtc_dtls_certificate->get_fingerprint());

    // We allows to mock the eip of server.
    if (true) {
        // TODO: Support multiple listen ports.
        int udp_port = 0;
        if (true) {
            string udp_host;
            string udp_hostport = _srs_config->get_rtc_server_listens().at(0);
            srs_net_split_for_listener(udp_hostport, udp_host, udp_port);
        }

        int tcp_port = 0;
        if (true) {
            string tcp_host;
            string tcp_hostport = _srs_config->get_rtc_server_tcp_listens().at(0);
            srs_net_split_for_listener(tcp_hostport, tcp_host, tcp_port);
        }

        string protocol = _srs_config->get_rtc_server_protocol();

        set<string> candidates = discover_candidates(ruc);
        for (set<string>::iterator it = candidates.begin(); it != candidates.end(); ++it) {
            string hostname;
            int uport = udp_port;
            srs_net_split_hostport(*it, hostname, uport);
            int tport = tcp_port;
            srs_net_split_hostport(*it, hostname, tport);

            if (protocol == "udp") {
                local_sdp.add_candidate("udp", hostname, uport, "host");
            } else if (protocol == "tcp") {
                local_sdp.add_candidate("tcp", hostname, tport, "host");
            } else {
                local_sdp.add_candidate("udp", hostname, uport, "host");
                local_sdp.add_candidate("tcp", hostname, tport, "host");
            }
        }

        vector<string> v = vector<string>(candidates.begin(), candidates.end());
        srs_trace("RTC: Use candidates %s, protocol=%s, tcp_port=%d, udp_port=%d",
                  srs_strings_join(v, ", ").c_str(), protocol.c_str(), tcp_port, udp_port);
    }

    // Setup the negotiate DTLS by config.
    local_sdp.session_negotiate_ = local_sdp.session_config_;

    // Setup the negotiate DTLS role.
    if (ruc->remote_sdp_.get_dtls_role() == "active") {
        local_sdp.session_negotiate_.dtls_role = "passive";
    } else if (ruc->remote_sdp_.get_dtls_role() == "passive") {
        local_sdp.session_negotiate_.dtls_role = "active";
    } else if (ruc->remote_sdp_.get_dtls_role() == "actpass") {
        local_sdp.session_negotiate_.dtls_role = local_sdp.session_config_.dtls_role;
    } else {
        // @see: https://tools.ietf.org/html/rfc4145#section-4.1
        // The default value of the setup attribute in an offer/answer exchange
        // is 'active' in the offer and 'passive' in the answer.
        local_sdp.session_negotiate_.dtls_role = "passive";
    }
    local_sdp.set_dtls_role(local_sdp.session_negotiate_.dtls_role);

    session->set_remote_sdp(ruc->remote_sdp_);
    // We must setup the local SDP, then initialize the session object.
    session->set_local_sdp(local_sdp);
    session->set_state_as_waiting_stun();

    // Before session initialize, we must setup the local SDP.
    if ((err = session->initialize(req, ruc->dtls_, ruc->srtp_, username)) != srs_success) {
        return srs_error_wrap(err, "init");
    }

    // We allows username is optional, but it never empty here.
    _srs_conn_manager->add_with_name(username, session);

    return err;
}

srs_error_t SrsServer::srs_update_rtc_sessions()
{
    srs_error_t err = srs_success;

    // Alive RTC sessions, for stat.
    int nn_rtc_conns = 0;

    // Check all sessions and dispose the dead sessions.
    for (int i = 0; i < (int)_srs_conn_manager->size(); i++) {
        SrsRtcConnection *session = dynamic_cast<SrsRtcConnection *>(_srs_conn_manager->at(i));
        // Ignore not session, or already disposing.
        if (!session || session->disposing_) {
            continue;
        }

        // Update stat if session is alive.
        if (session->is_alive()) {
            nn_rtc_conns++;
            continue;
        }

        SrsContextRestore(_srs_context->get_id());
        session->switch_to_context();

        string username = session->username();
        srs_trace("RTC: session destroy by timeout, username=%s", username.c_str());

        // Use manager to free session and notify other objects.
        _srs_conn_manager->remove(session);
    }

    // Ignore stats if no RTC connections.
    if (!nn_rtc_conns) {
        return err;
    }
    static char buf[128];

    string rpkts_desc;
    _srs_pps_rpkts->update();
    _srs_pps_rrtps->update();
    _srs_pps_rstuns->update();
    _srs_pps_rrtcps->update();
    if (_srs_pps_rpkts->r10s() || _srs_pps_rrtps->r10s() || _srs_pps_rstuns->r10s() || _srs_pps_rrtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", rpkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_rpkts->r10s(), _srs_pps_rrtps->r10s(), _srs_pps_rstuns->r10s(), _srs_pps_rrtcps->r10s());
        rpkts_desc = buf;
    }

    string spkts_desc;
    _srs_pps_spkts->update();
    _srs_pps_srtps->update();
    _srs_pps_sstuns->update();
    _srs_pps_srtcps->update();
    if (_srs_pps_spkts->r10s() || _srs_pps_srtps->r10s() || _srs_pps_sstuns->r10s() || _srs_pps_srtcps->r10s()) {
        snprintf(buf, sizeof(buf), ", spkts=(%d,rtp:%d,stun:%d,rtcp:%d)", _srs_pps_spkts->r10s(), _srs_pps_srtps->r10s(), _srs_pps_sstuns->r10s(), _srs_pps_srtcps->r10s());
        spkts_desc = buf;
    }

    string rtcp_desc;
    _srs_pps_pli->update();
    _srs_pps_twcc->update();
    _srs_pps_rr->update();
    if (_srs_pps_pli->r10s() || _srs_pps_twcc->r10s() || _srs_pps_rr->r10s()) {
        snprintf(buf, sizeof(buf), ", rtcp=(pli:%d,twcc:%d,rr:%d)", _srs_pps_pli->r10s(), _srs_pps_twcc->r10s(), _srs_pps_rr->r10s());
        rtcp_desc = buf;
    }

    string snk_desc;
    _srs_pps_snack->update();
    _srs_pps_snack2->update();
    _srs_pps_sanack->update();
    _srs_pps_svnack->update();
    if (_srs_pps_snack->r10s() || _srs_pps_sanack->r10s() || _srs_pps_svnack->r10s() || _srs_pps_snack2->r10s()) {
        snprintf(buf, sizeof(buf), ", snk=(%d,a:%d,v:%d,h:%d)", _srs_pps_snack->r10s(), _srs_pps_sanack->r10s(), _srs_pps_svnack->r10s(), _srs_pps_snack2->r10s());
        snk_desc = buf;
    }

    string rnk_desc;
    _srs_pps_rnack->update();
    _srs_pps_rnack2->update();
    _srs_pps_rhnack->update();
    _srs_pps_rmnack->update();
    if (_srs_pps_rnack->r10s() || _srs_pps_rnack2->r10s() || _srs_pps_rhnack->r10s() || _srs_pps_rmnack->r10s()) {
        snprintf(buf, sizeof(buf), ", rnk=(%d,%d,h:%d,m:%d)", _srs_pps_rnack->r10s(), _srs_pps_rnack2->r10s(), _srs_pps_rhnack->r10s(), _srs_pps_rmnack->r10s());
        rnk_desc = buf;
    }

    string loss_desc;
    SrsSnmpUdpStat *s = srs_get_udp_snmp_stat();
    if (s->rcv_buf_errors_delta || s->snd_buf_errors_delta) {
        snprintf(buf, sizeof(buf), ", loss=(r:%d,s:%d)", s->rcv_buf_errors_delta, s->snd_buf_errors_delta);
        loss_desc = buf;
    }

    string fid_desc;
    _srs_pps_ids->update();
    _srs_pps_fids->update();
    _srs_pps_fids_level0->update();
    _srs_pps_addrs->update();
    _srs_pps_fast_addrs->update();
    if (_srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s()) {
        snprintf(buf, sizeof(buf), ", fid=(id:%d,fid:%d,ffid:%d,addr:%d,faddr:%d)", _srs_pps_ids->r10s(), _srs_pps_fids->r10s(), _srs_pps_fids_level0->r10s(), _srs_pps_addrs->r10s(), _srs_pps_fast_addrs->r10s());
        fid_desc = buf;
    }

    srs_trace("RTC: Server conns=%u%s%s%s%s%s%s%s",
              nn_rtc_conns,
              rpkts_desc.c_str(), spkts_desc.c_str(), rtcp_desc.c_str(), snk_desc.c_str(), rnk_desc.c_str(), loss_desc.c_str(), fid_desc.c_str());

    return err;
}

srs_error_t SrsServer::on_tcp_client(ISrsListener *listener, srs_netfd_t stfd)
{
    srs_error_t err = do_on_tcp_client(listener, stfd);

    // We always try to close the stfd, because it should be NULL if it has been handled or closed.
    srs_close_stfd(stfd);

    return err;
}

srs_error_t SrsServer::do_on_tcp_client(ISrsListener *listener, srs_netfd_t &stfd)
{
    srs_error_t err = srs_success;

    int fd = srs_netfd_fileno(stfd);
    string ip = srs_get_peer_ip(fd);
    int port = srs_get_peer_port(fd);

    // Ignore if ip is empty, for example, load balancer keepalive.
    if (ip.empty()) {
        if (_srs_config->empty_ip_ok())
            return err;
        return srs_error_new(ERROR_SOCKET_GET_PEER_IP, "ignore empty ip, fd=%d", fd);
    }

    // Security or system flow control check.
    if ((err = on_before_connection("TCP", fd, ip, port)) != srs_success) {
        return srs_error_wrap(err, "check");
    }

    // Set to close the fd when forking, to avoid fd leak when start a process.
    // See https://github.com/ossrs/srs/issues/518
    if (true) {
        int val;
        if ((val = fcntl(fd, F_GETFD, 0)) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_GET_FILE_INFO, "fnctl F_GETFD error! fd=%d", fd);
        }
        val |= FD_CLOEXEC;
        if (fcntl(fd, F_SETFD, val) < 0) {
            return srs_error_new(ERROR_SYSTEM_PID_SET_FILE_INFO, "fcntl F_SETFD error! fd=%d", fd);
        }
    }

    // Covert handler to resource.
    ISrsResource *resource = NULL;

    // The context id may change during creating the bellow objects.
    SrsContextRestore(_srs_context->get_id());

    // From now on, we always handle the stfd, so we set the original one to NULL.
    srs_netfd_t stfd2 = stfd;
    stfd = NULL;

    // If reuse HTTP server with WebRTC TCP, peek to detect the client.
    if (reuse_rtc_over_server_ && (listener == http_listener_ || listener == https_listener_)) {
        SrsTcpConnection *skt = new SrsTcpConnection(stfd2);
        SrsBufferedReadWriter *io = new SrsBufferedReadWriter(skt);

        // Peek first N bytes to finger out the real client type.
        uint8_t b[10];
        int nn = sizeof(b);
        if ((err = io->peek((char *)b, &nn)) != srs_success) {
            srs_freep(io);
            srs_freep(skt);
            return srs_error_wrap(err, "peek");
        }

        // If first message is BindingRequest(00 01), prefixed with length(2B), it's WebRTC client. Generally, the frame
        // length minus message length should be 20, that is the header size of STUN is 20 bytes. For example:
        //      00 6c # Frame length: 0x006c = 108
        //      00 01 # Message Type: Binding Request(0x0001)
        //      00 58 # Message Length: 0x005 = 88
        //      21 12 a4 42 # Message Cookie: 0x2112a442
        //      48 32 6c 61 6b 42 35 71 42 35 4a 71 # Message Transaction ID: 12 bytes
        if (nn == 10 && b[0] == 0 && b[2] == 0 && b[3] == 1 && b[1] - b[5] == 20 && b[6] == 0x21 && b[7] == 0x12 && b[8] == 0xa4 && b[9] == 0x42) {
            resource = new SrsRtcTcpConn(io, ip, port);
        } else {
            string key = listener == https_listener_ ? _srs_config->get_https_stream_ssl_key() : "";
            string cert = listener == https_listener_ ? _srs_config->get_https_stream_ssl_cert() : "";
            resource = new SrsHttpxConn(_srs_conn_manager, io, http_server_, ip, port, key, cert);
        }
    }

    // Create resource by normal listeners.
    if (!resource) {
        if (listener == rtmp_listener_) {
            SrsRtmpTransport *transport = new SrsRtmpTransport(stfd2);
            resource = new SrsRtmpConn(this, transport, ip, port);
        } else if (listener == rtmps_listener_) {
            SrsRtmpTransport *transport = new SrsRtmpsTransport(stfd2);
            resource = new SrsRtmpConn(this, transport, ip, port);
        } else if (listener == api_listener_ || listener == apis_listener_) {
            string key = listener == apis_listener_ ? _srs_config->get_https_api_ssl_key() : "";
            string cert = listener == apis_listener_ ? _srs_config->get_https_api_ssl_cert() : "";
            resource = new SrsHttpxConn(_srs_conn_manager, new SrsTcpConnection(stfd2), http_api_mux_, ip, port, key, cert);
        } else if (listener == http_listener_ || listener == https_listener_) {
            string key = listener == https_listener_ ? _srs_config->get_https_stream_ssl_key() : "";
            string cert = listener == https_listener_ ? _srs_config->get_https_stream_ssl_cert() : "";
            resource = new SrsHttpxConn(_srs_conn_manager, new SrsTcpConnection(stfd2), http_server_, ip, port, key, cert);
        } else if (listener == webrtc_listener_) {
            resource = new SrsRtcTcpConn(new SrsTcpConnection(stfd2), ip, port);
#ifdef SRS_RTSP
        } else if (listener == rtsp_listener_) {
            resource = new SrsRtspConnection(_srs_conn_manager, new SrsTcpConnection(stfd2), ip, port);
#endif
        } else if (listener == exporter_listener_) {
            // TODO: FIXME: Maybe should support https metrics.
            resource = new SrsHttpxConn(_srs_conn_manager, new SrsTcpConnection(stfd2), http_api_mux_, ip, port, "", "");
        } else {
            srs_close_stfd(stfd2);
            srs_warn("Close for invalid fd=%d, ip=%s:%d", fd, ip.c_str(), port);
            return err;
        }
    }

    // For RTC TCP connection, use resource executor to manage the resource.
    SrsRtcTcpConn *raw_conn = dynamic_cast<SrsRtcTcpConn *>(resource);
    if (raw_conn) {
        SrsSharedResource<SrsRtcTcpConn> *conn = new SrsSharedResource<SrsRtcTcpConn>(raw_conn);
        SrsExecutorCoroutine *executor = new SrsExecutorCoroutine(_srs_conn_manager, conn, raw_conn, raw_conn);
        raw_conn->setup_owner(conn, executor, executor);
        if ((err = executor->start()) != srs_success) {
            srs_freep(executor);
            return srs_error_wrap(err, "start executor");
        }
        return err;
    }

    // Use connection manager to manage all the resources.
    srs_assert(resource);
    _srs_conn_manager->add(resource);

    // If connection is a resource to start, start a coroutine to handle it.
    // Note that conn is managed by conn_manager, so we don't need to free it.
    ISrsStartable *conn = dynamic_cast<ISrsStartable *>(resource);
    srs_assert(conn);
    if ((err = conn->start()) != srs_success) {
        return srs_error_wrap(err, "start conn coroutine");
    }

    return err;
}

srs_error_t SrsServer::on_before_connection(const char *label, int fd, const std::string &ip, int port)
{
    srs_error_t err = srs_success;

    // Failed if exceed the connection limitation.
    int max_connections = _srs_config->get_max_connections();

    if ((int)_srs_conn_manager->size() >= max_connections) {
        return srs_error_new(ERROR_EXCEED_CONNECTIONS, "drop %s fd=%d, ip=%s:%d, max=%d, cur=%d for exceed connection limits",
                             label, fd, ip.c_str(), port, max_connections, (int)_srs_conn_manager->size());
    }

    return err;
}

srs_error_t SrsServer::on_publish(ISrsRequest *r)
{
    srs_error_t err = srs_success;

    if ((err = http_server_->http_mount(r)) != srs_success) {
        return srs_error_wrap(err, "http mount");
    }

    SrsCoWorkers *coworkers = SrsCoWorkers::instance();
    if ((err = coworkers->on_publish(r)) != srs_success) {
        return srs_error_wrap(err, "coworkers");
    }

    return err;
}

void SrsServer::on_unpublish(ISrsRequest *r)
{
    http_server_->http_unmount(r);

    SrsCoWorkers *coworkers = SrsCoWorkers::instance();
    coworkers->on_unpublish(r);
}

SrsFastTimer *SrsServer::timer20ms()
{
    return timer20ms_;
}

SrsFastTimer *SrsServer::timer100ms()
{
    return timer100ms_;
}

SrsFastTimer *SrsServer::timer1s()
{
    return timer1s_;
}

SrsFastTimer *SrsServer::timer5s()
{
    return timer5s_;
}

srs_error_t SrsServer::on_timer(srs_utime_t interval)
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

    return err;
}
