//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_utest.hpp>

#include <srs_app_config.hpp>
#include <srs_app_log.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_server.hpp>
#include <srs_app_st.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_protocol_st.hpp>

#include <string>
using namespace std;

#include <sys/mman.h>
#include <sys/types.h>

#include <srs_app_factory.hpp>
#include <srs_app_srt_server.hpp>
#include <srt/srt.h>

// Temporary disk config.
std::string _srs_tmp_file_prefix = "/tmp/srs-utest-";
// Temporary network config.
std::string _srs_tmp_host = "127.0.0.1";
int _srs_tmp_port = 11935;
srs_utime_t _srs_tmp_timeout = (100 * SRS_UTIME_MILLISECONDS);

// kernel module.
ISrsLog *_srs_log = NULL;
ISrsContext *_srs_context = NULL;
// app module.
SrsConfig *_srs_config = NULL;
bool _srs_in_docker = false;
bool _srs_config_by_env = false;

// @global kernel factory.
ISrsKernelFactory *_srs_kernel_factory = new SrsFinalFactory();

// The binary name of SRS.
const char *_srs_binary = NULL;

#include <srs_app_st.hpp>

static void srs_srt_utest_null_log_handler(void *opaque, int level, const char *file, int line,
                                           const char *area, const char *message)
{
    // srt null log handler, do no print any log.
}

// Initialize global settings.
srs_error_t prepare_main()
{
    srs_error_t err = srs_success;

    if ((err = srs_global_initialize()) != srs_success) {
        return srs_error_wrap(err, "init global");
    }

    _srs_server = new SrsServer();

    srs_freep(_srs_log);
    _srs_log = new MockEmptyLog(SrsLogLevelError);

    if ((err = _srs_rtc_dtls_certificate->initialize()) != srs_success) {
        return srs_error_wrap(err, "rtc dtls certificate initialize");
    }

    srs_freep(_srs_context);
    _srs_context = new SrsThreadContext();

    if ((err = srs_srt_log_initialize()) != srs_success) {
        return srs_error_wrap(err, "srt log initialize");
    }

    // Prevent the output of srt logs in utest.
    srt_setloghandler(NULL, srs_srt_utest_null_log_handler);

    _srt_eventloop = new SrsSrtEventLoop();
    if ((err = _srt_eventloop->initialize()) != srs_success) {
        return srs_error_wrap(err, "srt poller initialize");
    }
    if ((err = _srt_eventloop->start()) != srs_success) {
        return srs_error_wrap(err, "srt poller start");
    }

    return err;
}

// We could do something in the main of utest.
// Copy from gtest-1.6.0/src/gtest_main.cc
GTEST_API_ int main(int argc, char **argv)
{
    srs_error_t err = srs_success;

    _srs_binary = argv[0];

    if ((err = prepare_main()) != srs_success) {
        fprintf(stderr, "Failed, %s\n", srs_error_desc(err).c_str());

        int ret = srs_error_code(err);
        srs_freep(err);
        return ret;
    }

    testing::InitGoogleTest(&argc, argv);
    int r0 = RUN_ALL_TESTS();

    return r0;
}

MockEmptyLog::MockEmptyLog(SrsLogLevel l)
{
    level_ = l;
}

MockEmptyLog::~MockEmptyLog()
{
}

void srs_bytes_print(char *pa, int size)
{
    for (int i = 0; i < size; i++) {
        char v = pa[i];
        printf("%#x ", v);
    }
    printf("\n");
}

// basic test and samples.
VOID TEST(SampleTest, FastSampleInt64Test)
{
    EXPECT_EQ(1, (int)sizeof(int8_t));
    EXPECT_EQ(2, (int)sizeof(int16_t));
    EXPECT_EQ(4, (int)sizeof(int32_t));
    EXPECT_EQ(8, (int)sizeof(int64_t));
}

VOID TEST(SampleTest, FastSampleMacrosTest)
{
    EXPECT_TRUE(1);
    EXPECT_FALSE(0);

    EXPECT_EQ(1, 1); // ==
    EXPECT_NE(1, 2); // !=
    EXPECT_LE(1, 2); // <=
    EXPECT_LT(1, 2); // <
    EXPECT_GE(2, 1); // >=
    EXPECT_GT(2, 1); // >

    EXPECT_STREQ("winlin", "winlin");
    EXPECT_STRNE("winlin", "srs");
    EXPECT_STRCASEEQ("winlin", "Winlin");
    EXPECT_STRCASENE("winlin", "srs");

    EXPECT_FLOAT_EQ(1.0, 1.000000000000001);
    EXPECT_DOUBLE_EQ(1.0, 1.0000000000000001);
    EXPECT_NEAR(10, 15, 5);
}

VOID TEST(SampleTest, StringEQTest)
{
    string str = "100";
    EXPECT_TRUE("100" == str);
    EXPECT_EQ("100", str);
    EXPECT_STREQ("100", str.c_str());
}

class MockSrsContextId
{
public:
    MockSrsContextId()
    {
        bind_ = NULL;
    }
    MockSrsContextId(const MockSrsContextId &cp)
    {
        bind_ = NULL;
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
    }
    MockSrsContextId &operator=(const MockSrsContextId &cp)
    {
        srs_freep(bind_);
        if (cp.bind_) {
            bind_ = cp.bind_->copy();
        }
        return *this;
    }
    virtual ~MockSrsContextId()
    {
        srs_freep(bind_);
    }

public:
    MockSrsContextId *copy() const
    {
        MockSrsContextId *cp = new MockSrsContextId();
        if (bind_) {
            cp->bind_ = bind_->copy();
        }
        return cp;
    }

private:
    MockSrsContextId *bind_;
};

VOID TEST(SampleTest, ContextTest)
{
    MockSrsContextId cid;
    cid.bind_ = new MockSrsContextId();

    static std::map<int, MockSrsContextId> cache;
    cache[0] = cid;
    cache[0] = cid;
}

MockProtectedBuffer::MockProtectedBuffer() : raw_memory_(NULL), size_(0), data_(NULL)
{
}

MockProtectedBuffer::~MockProtectedBuffer()
{
    if (size_ && raw_memory_) {
        long page_size = sysconf(_SC_PAGESIZE);
        munmap(raw_memory_, page_size * 2);
    }
}

int MockProtectedBuffer::alloc(int size)
{
    srs_assert(!raw_memory_);

    long page_size = sysconf(_SC_PAGESIZE);
    if (size >= page_size)
        return -1;

    char *data = (char *)mmap(NULL, page_size * 2, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (data == MAP_FAILED) {
        return -1;
    }

    size_ = size;
    raw_memory_ = data;
    data_ = data + page_size - size;

    int r0 = mprotect(data + page_size, page_size, PROT_NONE);
    if (r0 < 0) {
        return r0;
    }

    return 0;
}

SrsCoroutineChan::SrsCoroutineChan()
{
    trd_ = NULL;
    lock_ = srs_mutex_new();
}

SrsCoroutineChan::~SrsCoroutineChan()
{
    srs_mutex_destroy(lock_);
}

SrsCoroutineChan &SrsCoroutineChan::push(void *value)
{
    SrsLocker(&lock_);

    args_.push_back(value);
    return *this;
}

void *SrsCoroutineChan::pop()
{
    SrsLocker(&lock_);

    void *arg = *args_.begin();
    args_.erase(args_.begin());
    return arg;
}

SrsCoroutineChan *SrsCoroutineChan::copy()
{
    SrsLocker(&lock_);

    SrsCoroutineChan *cp = new SrsCoroutineChan();
    cp->args_ = args_;
    cp->trd_ = trd_;
    return cp;
}

extern string mock_http_response(int status, string content);

SrsHttpTestServer::SrsHttpTestServer(string response_body) : response_body_(response_body)
{
    trd_ = new SrsSTCoroutine("http-test", this);
    fd_ = NULL;
    ip_ = "127.0.0.1";
    port_ = srs_rand_integer(30000, 60000);
}

SrsHttpTestServer::~SrsHttpTestServer()
{
    close();
    srs_freep(trd_);
    srs_close_stfd(fd_);
}

srs_error_t SrsHttpTestServer::start()
{
    srs_error_t err = srs_success;

    if ((err = srs_tcp_listen(ip_, port_, &fd_)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip_.c_str(), port_);
    }

    return trd_->start();
}

void SrsHttpTestServer::close()
{
    if (trd_) {
        trd_->stop();
    }
    srs_close_stfd(fd_);
}

string SrsHttpTestServer::url()
{
    return "http://" + ip_ + ":" + srs_strconv_format_int(port_);
}

int SrsHttpTestServer::get_port()
{
    return port_;
}

srs_error_t SrsHttpTestServer::cycle()
{
    srs_error_t err = srs_success;

    srs_netfd_t cfd = srs_accept(fd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
    if (cfd == NULL) {
        return err;
    }

    err = do_cycle(cfd);
    srs_close_stfd(cfd);
    srs_freep(err);

    return err;
}

srs_error_t SrsHttpTestServer::do_cycle(srs_netfd_t cfd)
{
    srs_error_t err = srs_success;

    SrsStSocket skt(cfd);
    skt.set_recv_timeout(1 * SRS_UTIME_SECONDS);
    skt.set_send_timeout(1 * SRS_UTIME_SECONDS);

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return err;
        }

        char buf[1024];
        if ((err = skt.read(buf, 1024, NULL)) != srs_success) {
            return err;
        }

        // Generate proper HTTP response
        string res = mock_http_response(200, response_body_);
        if ((err = skt.write((char *)res.data(), (int)res.length(), NULL)) != srs_success) {
            return err;
        }
    }

    return err;
}

SrsHttpsTestServer::SrsHttpsTestServer(string response_body, string key_file, string cert_file)
    : response_body_(response_body), ssl_key_file_(key_file), ssl_cert_file_(cert_file)
{
    trd_ = new SrsFastCoroutine("https-test", this);
    fd_ = NULL;
    ip_ = "127.0.0.1";
    port_ = srs_rand_integer(30000, 60000);
}

SrsHttpsTestServer::~SrsHttpsTestServer()
{
    close();
    srs_freep(trd_);
}

srs_error_t SrsHttpsTestServer::start()
{
    srs_error_t err = srs_success;

    if ((err = srs_tcp_listen(ip_, port_, &fd_)) != srs_success) {
        return srs_error_wrap(err, "listen %s:%d", ip_.c_str(), port_);
    }

    if ((err = trd_->start()) != srs_success) {
        return srs_error_wrap(err, "start coroutine");
    }

    return err;
}

void SrsHttpsTestServer::close()
{
    if (trd_) {
        trd_->stop();
    }
    if (fd_) {
        srs_close_stfd(fd_);
        fd_ = NULL;
    }
}

string SrsHttpsTestServer::url()
{
    return "https://" + ip_ + ":" + srs_strconv_format_int(port_);
}

int SrsHttpsTestServer::get_port()
{
    return port_;
}

srs_error_t SrsHttpsTestServer::cycle()
{
    srs_error_t err = srs_success;

    while (true) {
        if ((err = trd_->pull()) != srs_success) {
            return srs_error_wrap(err, "pull");
        }

        srs_netfd_t client_fd = srs_accept(fd_, NULL, NULL, SRS_UTIME_NO_TIMEOUT);
        if (client_fd == NULL) {
            return srs_error_new(ERROR_SOCKET_ACCEPT, "accept failed");
        }

        if ((err = handle_client(client_fd)) != srs_success) {
            srs_warn("handle client failed, err=%s", srs_error_desc(err).c_str());
            srs_freep(err);
        }
    }

    return err;
}

srs_error_t SrsHttpsTestServer::handle_client(srs_netfd_t client_fd)
{
    srs_error_t err = srs_success;

    SrsStSocket *skt = new SrsStSocket(client_fd);
    SrsUniquePtr<SrsStSocket> skt_uptr(skt);

    // Create SSL connection
    SrsSslConnection *ssl = new SrsSslConnection(skt);
    SrsUniquePtr<SrsSslConnection> ssl_uptr(ssl);

    // Perform SSL handshake
    if ((err = ssl->handshake(ssl_key_file_, ssl_cert_file_)) != srs_success) {
        return srs_error_wrap(err, "ssl handshake");
    }

    // Read HTTP request (simplified - just read some data)
    char buf[4096];
    ssize_t nread = 0;
    if ((err = ssl->read(buf, sizeof(buf), &nread)) != srs_success) {
        return srs_error_wrap(err, "read request");
    }

    // Send HTTP response
    string response = mock_http_response(200, response_body_);
    if ((err = ssl->write((void*)response.data(), response.length(), NULL)) != srs_success) {
        return srs_error_wrap(err, "write response");
    }

    return err;
}
