//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_PUBLIC_SHARED_HPP
#define SRS_UTEST_PUBLIC_SHARED_HPP

// Before define the private/protected, we must include some system header files.
// Or it may fail with:
//      redeclared with different access struct __xfer_bufptrs
// @see https://stackoverflow.com/questions/47839718/sstream-redeclared-with-public-access-compiler-error
#include "gtest/gtest.h"

// Public all private and protected members.
#define private public
#define protected public

/*
#include <srs_utest.hpp>
*/
#include <srs_core.hpp>

#include <string>
using namespace std;

#include <srs_app_log.hpp>
#include <srs_kernel_stream.hpp>

// we add an empty macro for upp to show the smart tips.
#define VOID

// Temporary disk config.
extern std::string _srs_tmp_file_prefix;
// Temporary network config.
extern std::string _srs_tmp_host;
extern int _srs_tmp_port;
extern srs_utime_t _srs_tmp_timeout;

// For errors.
// @remark we directly delete the err, because we allow user to append message if fail.
#define HELPER_EXPECT_SUCCESS(x)                                \
    if ((err = x) != srs_success)                               \
        fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    if (err != srs_success)                                     \
        delete err;                                             \
    EXPECT_TRUE(srs_success == err)
#define HELPER_EXPECT_FAILED(x)   \
    if ((err = x) != srs_success) \
        delete err;               \
    EXPECT_TRUE(srs_success != err)

// For errors, assert.
// @remark we directly delete the err, because we allow user to append message if fail.
#define HELPER_ASSERT_SUCCESS(x)                                \
    if ((err = x) != srs_success)                               \
        fprintf(stderr, "err %s", srs_error_desc(err).c_str()); \
    if (err != srs_success)                                     \
        delete err;                                             \
    ASSERT_TRUE(srs_success == err)
#define HELPER_ASSERT_FAILED(x)   \
    if ((err = x) != srs_success) \
        delete err;               \
    ASSERT_TRUE(srs_success != err)

// For init array data.
#define HELPER_ARRAY_INIT(buf, sz, val)        \
    for (int _iii = 0; _iii < (int)sz; _iii++) \
    (buf)[_iii] = val

// Dump simple stream to string.
#define HELPER_BUFFER2STR(io) \
    string((const char *)(io)->bytes(), (size_t)(io)->length())

// Covert uint8_t array to string.
#define HELPER_ARR2STR(arr, size) \
    string((char *)(arr), (int)size)

// the asserts of gtest:
//    * {ASSERT|EXPECT}_EQ(expected, actual): Tests that expected == actual
//    * {ASSERT|EXPECT}_NE(v1, v2):           Tests that v1 != v2
//    * {ASSERT|EXPECT}_LT(v1, v2):           Tests that v1 < v2
//    * {ASSERT|EXPECT}_LE(v1, v2):           Tests that v1 <= v2
//    * {ASSERT|EXPECT}_GT(v1, v2):           Tests that v1 > v2
//    * {ASSERT|EXPECT}_GE(v1, v2):           Tests that v1 >= v2
//    * {ASSERT|EXPECT}_STREQ(s1, s2):     Tests that s1 == s2
//    * {ASSERT|EXPECT}_STRNE(s1, s2):     Tests that s1 != s2
//    * {ASSERT|EXPECT}_STRCASEEQ(s1, s2): Tests that s1 == s2, ignoring case
//    * {ASSERT|EXPECT}_STRCASENE(s1, s2): Tests that s1 != s2, ignoring case
//    * {ASSERT|EXPECT}_FLOAT_EQ(expected, actual): Tests that two float values are almost equal.
//    * {ASSERT|EXPECT}_DOUBLE_EQ(expected, actual): Tests that two double values are almost equal.
//    * {ASSERT|EXPECT}_NEAR(v1, v2, abs_error): Tests that v1 and v2 are within the given distance to each other.

// print the bytes.
void srs_bytes_print(char *pa, int size);

class MockEmptyLog : public SrsFileLog
{
public:
    MockEmptyLog(SrsLogLevel l);
    virtual ~MockEmptyLog();
};

// To test the memory corruption, we protect the memory by mprotect.
//          MockProtectedBuffer buffer;
//          if (buffer.alloc(8)) { EXPECT_TRUE(false); return; }
// Crash when write beyond the data:
//          buffer.data_[0] = 0; // OK
//          buffer.data_[7] = 0; // OK
//          buffer.data_[8] = 0; // Crash
// Crash when read beyond the data:
//          char v = buffer.data_[0]; // OK
//          char v = buffer.data_[7]; // OK
//          char v = buffer.data_[8]; // Crash
// @remark The size of memory to allocate, should smaller than page size, generally 4096 bytes.
class MockProtectedBuffer
{
private:
    char *raw_memory_;

public:
    int size_;
    // Should use this as data.
    char *data_;

public:
    MockProtectedBuffer();
    virtual ~MockProtectedBuffer();
    // Return 0 for success.
    int alloc(int size);
};

// The chan for anonymous coroutine to share variables.
// The chan never free the args, you must manage the memory.
class SrsCoroutineChan
{
private:
    std::vector<void *> args_;
    srs_mutex_t lock_;

public:
    SrsCoroutineChan();
    virtual ~SrsCoroutineChan();

public:
    SrsCoroutineChan &push(void *value);
    void *pop();
    SrsCoroutineChan *copy();
};

// A helper to create a anonymous coroutine like goroutine in Go.
// * The context is used to share variables between coroutines.
// * The id is used to identify the coroutine.
// * The code_block is the code to run in the coroutine.
//
// The correct way is to avoid the block, unless you intend to do it,
// so you should create in the same scope, and use id to distinguish them.
// For example:
//        SrsCoroutineChan ctx;
//
//        SRS_COROUTINE_GO_IMPL(&ctx, coroutine1, {
//            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//        });
//
//        SRS_COROUTINE_GO_IMPL(&ctx, coroutine2, {
//            srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//        });
//
//        // It won't wait for the coroutine to terminate.
//        // So you will expect to run to here immediately.
//
// CAUTION: Note that if use a block to run the coroutine, it will
// stop and wait for the coroutine to terminate. So it maybe crash
// for the current thread is interrupted and stopping, such as the
// ctx.pop() will crash for requiring a lock on a stopping thread.
// For example:
//        SrsCoroutineChan ctx;
//
//        // Generally we SHOULD NOT do this, unless you intend to.
//        if (true) {
//            SRS_COROUTINE_GO_IMPL(&ctx, coroutine, {
//                srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//            });
//        }
//        if (true) {
//            SRS_COROUTINE_GO_IMPL(&ctx, coroutine, {
//                srs_usleep(1000 * SRS_UTIME_MILLISECONDS);
//            });
//        }
//
//        // The coroutine will be stopped and wait for it to terminate.
//        // So maybe it won't execute all your code there.
//
// Enjoiy the sugar for coroutines.
#define SRS_COROUTINE_GO_IMPL(context, id, code_block)                            \
    class AnonymousCoroutineHandler##id : public ISrsCoroutineHandler             \
    {                                                                             \
    private:                                                                      \
        SrsCoroutineChan *ctx_;                                                   \
                                                                                  \
    public:                                                                       \
        AnonymousCoroutineHandler##id(SrsCoroutineChan *c)                        \
        {                                                                         \
            /* Copy the context so that we can pop it in different coroutines. */ \
            ctx_ = c->copy();                                                     \
        }                                                                         \
        ~AnonymousCoroutineHandler##id()                                          \
        {                                                                         \
            srs_freep(ctx_);                                                      \
        }                                                                         \
                                                                                  \
    public:                                                                       \
        virtual srs_error_t cycle()                                               \
        {                                                                         \
            SrsCoroutineChan &ctx = *ctx_;                                        \
            (void)ctx;                                                            \
            code_block;                                                           \
            return srs_success;                                                   \
        }                                                                         \
    };                                                                            \
    AnonymousCoroutineHandler##id handler##id(context);                           \
    SrsSTCoroutine st##id("anonymous", &handler##id);                             \
    srs_error_t err_coroutine##id = st##id.start();                               \
    srs_assert(err_coroutine##id == srs_success)

// A helper to create a anonymous coroutine like goroutine in Go.
// For example:
//        SRS_COROUTINE_GO({
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO(code_block) \
    SrsCoroutineChan context##id;    \
    SRS_COROUTINE_GO_IMPL(&context##id, coroutine0, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the id, it allows you to create multiple coroutines.
// For example:
//        SRS_COROUTINE_GO2(coroutine1, {
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
//        SRS_COROUTINE_GO2(coroutine2, {
//            srs_usleep(1 * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO2(id, code_block) \
    SrsCoroutineChan context##id;         \
    SRS_COROUTINE_GO_IMPL(&context##id, id, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the context, it allows you to share variables between coroutines.
// For example:
//        SrsCoroutineChan ctx;
//        ctx.push(1);
//        SRS_COROUTINE_GO_CTX(ctx, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO_CTX(ctx, code_block) \
    SRS_COROUTINE_GO_IMPL(ctx, coroutine0, code_block)

// A helper to create a anonymous coroutine like goroutine in Go.
// With the context and id, it allows you to create multiple coroutines.
// For example:
//        SrsCoroutineChan ctx;
//        ctx.push(1);
//        SRS_COROUTINE_GO_CTX2(ctx, coroutine1, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
//        SRS_COROUTINE_GO_CTX2(ctx, coroutine2, {
//            int v = (int)ctx.pop();
//            srs_usleep(v * SRS_UTIME_MILLISECONDS);
//        });
#define SRS_COROUTINE_GO_CTX2(ctx, id, code_block) \
    SRS_COROUTINE_GO_IMPL(ctx, id, code_block)

#endif
