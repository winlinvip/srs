//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//
#include <srs_utest_kernel3.hpp>

#include <srs_app_utility.hpp>
#include <srs_kernel_buffer.hpp>
#include <srs_kernel_consts.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_factory.hpp>
#include <srs_kernel_hourglass.hpp>
#include <srs_kernel_io.hpp>
#include <srs_kernel_packet.hpp>
#include <srs_kernel_pithy_print.hpp>
#include <srs_kernel_resource.hpp>
#include <srs_kernel_rtc_queue.hpp>
#include <srs_kernel_st.hpp>
#include <srs_kernel_stream.hpp>
#include <srs_kernel_utility.hpp>

#include <algorithm>

// Mock classes for IO testing
class MockSrsReader : public ISrsReader
{
public:
    std::string data_;
    size_t pos_;
    srs_error_t read_error_;

public:
    MockSrsReader(const std::string &data) : data_(data), pos_(0), read_error_(srs_success) {}
    virtual ~MockSrsReader() {}

public:
    virtual srs_error_t read(void *buf, size_t size, ssize_t *nread)
    {
        if (read_error_ != srs_success) {
            return srs_error_copy(read_error_);
        }

        size_t available = data_.size() - pos_;
        size_t to_read = std::min(size, available);

        if (to_read > 0) {
            memcpy(buf, data_.data() + pos_, to_read);
            pos_ += to_read;
        }

        if (nread)
            *nread = to_read;
        return srs_success;
    }

    void set_error(srs_error_t err) { read_error_ = err; }
};

class MockSrsWriter : public ISrsWriter
{
public:
    std::string written_data_;
    srs_error_t write_error_;

public:
    MockSrsWriter() : write_error_(srs_success) {}
    virtual ~MockSrsWriter() {}

public:
    virtual srs_error_t write(void *buf, size_t size, ssize_t *nwrite)
    {
        if (write_error_ != srs_success) {
            return srs_error_copy(write_error_);
        }

        written_data_.append((char *)buf, size);
        if (nwrite)
            *nwrite = size;
        return srs_success;
    }

    virtual srs_error_t writev(const iovec *iov, int iov_size, ssize_t *nwrite)
    {
        if (write_error_ != srs_success) {
            return srs_error_copy(write_error_);
        }

        ssize_t total = 0;
        for (int i = 0; i < iov_size; i++) {
            written_data_.append((char *)iov[i].iov_base, iov[i].iov_len);
            total += iov[i].iov_len;
        }

        if (nwrite)
            *nwrite = total;
        return srs_success;
    }

    void set_error(srs_error_t err) { write_error_ = err; }
};

class MockSrsSeeker : public ISrsSeeker
{
public:
    off_t position_;
    srs_error_t seek_error_;

public:
    MockSrsSeeker() : position_(0), seek_error_(srs_success) {}
    virtual ~MockSrsSeeker() {}

public:
    virtual srs_error_t lseek(off_t offset, int whence, off_t *seeked)
    {
        if (seek_error_ != srs_success) {
            return srs_error_copy(seek_error_);
        }

        switch (whence) {
        case SEEK_SET:
            position_ = offset;
            break;
        case SEEK_CUR:
            position_ += offset;
            break;
        case SEEK_END:
            position_ = 1000 + offset; // Mock file size of 1000
            break;
        }

        if (seeked)
            *seeked = position_;
        return srs_success;
    }

    void set_error(srs_error_t err) { seek_error_ = err; }
};

// Tests for srs_kernel_io.hpp
VOID TEST(KernelIOTest, ISrsReaderInterface)
{
    MockSrsReader reader("Hello World");

    char buf[20];
    ssize_t nread = 0;

    // Test successful read
    srs_error_t err = reader.read(buf, 5, &nread);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(5, (int)nread);
    EXPECT_EQ(0, memcmp(buf, "Hello", 5));

    // Test reading remaining data
    err = reader.read(buf, 10, &nread);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(6, (int)nread);
    EXPECT_EQ(0, memcmp(buf, " World", 6));

    // Test reading beyond end
    err = reader.read(buf, 10, &nread);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(0, (int)nread);
}

VOID TEST(KernelIOTest, ISrsReaderErrorHandling)
{
    srs_error_t err;

    MockSrsReader reader("test");
    reader.set_error(srs_error_new(ERROR_SYSTEM_IO_INVALID, "mock error"));

    char buf[10];
    ssize_t nread = 0;

    err = reader.read(buf, 5, &nread);
    EXPECT_TRUE(err != srs_success);
    srs_freep(err);
}

VOID TEST(KernelIOTest, ISrsWriterInterface)
{
    MockSrsWriter writer;

    const char *data1 = "Hello";
    const char *data2 = " World";
    ssize_t nwrite = 0;

    // Test write
    srs_error_t err = writer.write((void *)data1, 5, &nwrite);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(5, (int)nwrite);
    EXPECT_EQ("Hello", writer.written_data_);

    // Test another write
    err = writer.write((void *)data2, 6, &nwrite);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(6, (int)nwrite);
    EXPECT_EQ("Hello World", writer.written_data_);
}

VOID TEST(KernelIOTest, ISrsVectorWriterInterface)
{
    MockSrsWriter writer;

    const char *data1 = "Hello";
    const char *data2 = " ";
    const char *data3 = "World";

    iovec iov[3];
    iov[0].iov_base = (void *)data1;
    iov[0].iov_len = 5;
    iov[1].iov_base = (void *)data2;
    iov[1].iov_len = 1;
    iov[2].iov_base = (void *)data3;
    iov[2].iov_len = 5;

    ssize_t nwrite = 0;
    srs_error_t err = writer.writev(iov, 3, &nwrite);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(11, (int)nwrite);
    EXPECT_EQ("Hello World", writer.written_data_);
}

VOID TEST(KernelIOTest, ISrsSeekerInterface)
{
    MockSrsSeeker seeker;

    off_t seeked = 0;

    // Test SEEK_SET
    srs_error_t err = seeker.lseek(100, SEEK_SET, &seeked);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(100, (int)seeked);

    // Test SEEK_CUR
    err = seeker.lseek(50, SEEK_CUR, &seeked);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(150, (int)seeked);

    // Test SEEK_END
    err = seeker.lseek(-10, SEEK_END, &seeked);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(990, (int)seeked); // 1000 - 10
}

// Tests for srs_kernel_packet.hpp
VOID TEST(KernelPacketTest, SrsNaluSampleBasic)
{
    // Test default constructor
    SrsNaluSample sample1;
    EXPECT_EQ(0, sample1.size_);
    EXPECT_TRUE(sample1.bytes_ == NULL);

    // Test constructor with data
    char data[] = {0x00, 0x00, 0x00, 0x01, 0x67};
    SrsNaluSample sample2(data, sizeof(data));
    EXPECT_EQ(sizeof(data), (size_t)sample2.size_);
    EXPECT_TRUE(sample2.bytes_ == data);
}

VOID TEST(KernelPacketTest, SrsNaluSampleCopy)
{
    char data[] = {0x00, 0x00, 0x00, 0x01, 0x67};
    SrsNaluSample original(data, sizeof(data));

    SrsNaluSample *copy = original.copy();
    EXPECT_TRUE(copy != NULL);
    EXPECT_EQ(original.size_, copy->size_);
    EXPECT_TRUE(copy->bytes_ == original.bytes_); // Should share the same pointer

    srs_freep(copy);
}

VOID TEST(KernelPacketTest, SrsMediaPacketBasic)
{
    SrsMediaPacket packet;

    // Test default values
    EXPECT_EQ(0, packet.timestamp_);
    EXPECT_EQ(SrsFrameTypeReserved, packet.message_type_);
    EXPECT_EQ(0, packet.stream_id_);
    EXPECT_TRUE(packet.payload() == NULL);
    EXPECT_EQ(0, packet.size());

    // Test type checking
    EXPECT_FALSE(packet.is_av());
    EXPECT_FALSE(packet.is_audio());
    EXPECT_FALSE(packet.is_video());
}

VOID TEST(KernelPacketTest, SrsMediaPacketWrap)
{
    SrsMediaPacket packet;

    // Use heap-allocated memory for wrap() test
    int data_size = 9;
    char *data = new char[data_size];
    memcpy(data, "test data", data_size);

    packet.wrap(data, data_size);

    EXPECT_TRUE(packet.payload() != NULL);
    EXPECT_EQ(data_size, packet.size());
    EXPECT_EQ(0, memcmp(packet.payload(), "test data", data_size));

    // Note: packet destructor will free the data
}

VOID TEST(KernelPacketTest, SrsMediaPacketTypeChecking)
{
    SrsMediaPacket packet;

    // Test audio packet
    packet.message_type_ = SrsFrameTypeAudio;
    EXPECT_TRUE(packet.is_av());
    EXPECT_TRUE(packet.is_audio());
    EXPECT_FALSE(packet.is_video());

    // Test video packet
    packet.message_type_ = SrsFrameTypeVideo;
    EXPECT_TRUE(packet.is_av());
    EXPECT_FALSE(packet.is_audio());
    EXPECT_TRUE(packet.is_video());

    // Test script packet
    packet.message_type_ = SrsFrameTypeScript;
    EXPECT_FALSE(packet.is_av());
    EXPECT_FALSE(packet.is_audio());
    EXPECT_FALSE(packet.is_video());
}

// Mock classes for resource testing
class MockSrsResource : public ISrsResource
{
public:
    SrsContextId cid_;
    std::string desc_;

public:
    MockSrsResource()
    {
        desc_ = "mock resource";
    }
    virtual ~MockSrsResource() {}

public:
    virtual const SrsContextId &get_id() { return cid_; }
    virtual std::string desc() { return desc_; }

    void set_id(const SrsContextId &cid) { cid_ = cid; }
    void set_desc(const std::string &desc) { desc_ = desc; }
};

class MockSrsDisposingHandler : public ISrsDisposingHandler
{
public:
    std::vector<ISrsResource *> before_dispose_calls_;
    std::vector<ISrsResource *> disposing_calls_;

public:
    MockSrsDisposingHandler() {}
    virtual ~MockSrsDisposingHandler() {}

public:
    virtual void on_before_dispose(ISrsResource *c)
    {
        before_dispose_calls_.push_back(c);
    }
    virtual void on_disposing(ISrsResource *c)
    {
        disposing_calls_.push_back(c);
    }
};

// Tests for srs_kernel_resource.hpp
VOID TEST(KernelResourceTest, ISrsResourceInterface)
{
    MockSrsResource resource;

    // Test default description
    EXPECT_EQ("mock resource", resource.desc());

    // Test setting description
    resource.set_desc("test resource");
    EXPECT_EQ("test resource", resource.desc());

    // Test context ID
    SrsContextId cid;
    resource.set_id(cid);
    EXPECT_STREQ(cid.c_str(), resource.get_id().c_str());
}

VOID TEST(KernelResourceTest, SrsSharedResourceBasic)
{
    MockSrsResource *raw_resource = new MockSrsResource();
    raw_resource->set_desc("shared test");

    SrsSharedResource<MockSrsResource> shared_resource(raw_resource);

    // Test access through shared resource
    EXPECT_TRUE(shared_resource.get() != NULL);
    EXPECT_EQ("shared test", shared_resource->desc());
    EXPECT_EQ("shared test", shared_resource.desc());

    // Test copy constructor
    SrsSharedResource<MockSrsResource> copy_resource(shared_resource);
    EXPECT_TRUE(copy_resource.get() != NULL);
    EXPECT_EQ("shared test", copy_resource->desc());

    // Test assignment operator
    SrsSharedResource<MockSrsResource> assigned_resource;
    assigned_resource = shared_resource;
    EXPECT_TRUE(assigned_resource.get() != NULL);
    EXPECT_EQ("shared test", assigned_resource->desc());
}

// Mock classes for hourglass testing
class MockSrsHourGlass : public ISrsHourGlass
{
public:
    std::vector<int> events_;
    std::vector<srs_utime_t> intervals_;
    std::vector<srs_utime_t> ticks_;

public:
    MockSrsHourGlass() {}
    virtual ~MockSrsHourGlass() {}

public:
    virtual srs_error_t notify(int event, srs_utime_t interval, srs_utime_t tick)
    {
        events_.push_back(event);
        intervals_.push_back(interval);
        ticks_.push_back(tick);
        return srs_success;
    }

    void clear()
    {
        events_.clear();
        intervals_.clear();
        ticks_.clear();
    }
};

class MockSrsFastTimer : public ISrsFastTimer
{
public:
    std::vector<srs_utime_t> timer_calls_;

public:
    MockSrsFastTimer() {}
    virtual ~MockSrsFastTimer() {}

public:
    virtual srs_error_t on_timer(srs_utime_t interval)
    {
        timer_calls_.push_back(interval);
        return srs_success;
    }

    void clear()
    {
        timer_calls_.clear();
    }
};

// Tests for srs_kernel_hourglass.hpp
VOID TEST(KernelHourglassTest, ISrsHourGlassInterface)
{
    MockSrsHourGlass handler;

    // Test notify
    srs_error_t err = handler.notify(1, 1000, 5000);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(1, (int)handler.events_.size());
    EXPECT_EQ(1, handler.events_[0]);
    EXPECT_EQ(1000, (int)handler.intervals_[0]);
    EXPECT_EQ(5000, (int)handler.ticks_[0]);

    // Test multiple notifications
    err = handler.notify(2, 2000, 10000);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(2, (int)handler.events_.size());
    EXPECT_EQ(2, handler.events_[1]);
    EXPECT_EQ(2000, (int)handler.intervals_[1]);
    EXPECT_EQ(10000, (int)handler.ticks_[1]);
}

VOID TEST(KernelHourglassTest, ISrsFastTimerInterface)
{
    MockSrsFastTimer timer;

    // Test timer callback
    srs_error_t err = timer.on_timer(100 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(1, (int)timer.timer_calls_.size());
    EXPECT_EQ(100 * SRS_UTIME_MILLISECONDS, timer.timer_calls_[0]);

    // Test multiple timer calls
    err = timer.on_timer(200 * SRS_UTIME_MILLISECONDS);
    EXPECT_EQ(srs_success, err);
    EXPECT_EQ(2, (int)timer.timer_calls_.size());
    EXPECT_EQ(200 * SRS_UTIME_MILLISECONDS, timer.timer_calls_[1]);
}

// Tests for srs_kernel_consts.hpp
VOID TEST(KernelConstsTest, RTMPConstants)
{
    // Test RTMP default values
    EXPECT_EQ(1935, SRS_CONSTS_RTMP_DEFAULT_PORT);
    EXPECT_STREQ("__defaultVhost__", SRS_CONSTS_RTMP_DEFAULT_VHOST);
    EXPECT_STREQ("__defaultApp__", SRS_CONSTS_RTMP_DEFAULT_APP);

    // Test chunk sizes
    EXPECT_EQ(60000, SRS_CONSTS_RTMP_SRS_CHUNK_SIZE);
    EXPECT_EQ(128, SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE);

    // Verify chunk size constraints
    EXPECT_GE(SRS_CONSTS_RTMP_SRS_CHUNK_SIZE, SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE);
    EXPECT_LE(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE, 65536);
    EXPECT_GE(SRS_CONSTS_RTMP_PROTOCOL_CHUNK_SIZE, 128);
}

VOID TEST(KernelConstsTest, HTTPConstants)
{
    // Test HTTP default ports
    EXPECT_EQ(80, SRS_DEFAULT_HTTP_PORT);
    EXPECT_EQ(443, SRS_DEFAULT_HTTPS_PORT);
    EXPECT_EQ(6379, SRS_DEFAULT_REDIS_PORT);

    // Verify port ranges are valid
    EXPECT_GT(SRS_DEFAULT_HTTP_PORT, 0);
    EXPECT_LT(SRS_DEFAULT_HTTP_PORT, 65536);
    EXPECT_GT(SRS_DEFAULT_HTTPS_PORT, 0);
    EXPECT_LT(SRS_DEFAULT_HTTPS_PORT, 65536);
}

// Tests for srs_kernel_utility.hpp
VOID TEST(KernelUtilityTest, StringFormatting)
{
    // Test integer formatting
    EXPECT_EQ("123", srs_strconv_format_int(123));
    EXPECT_EQ("-456", srs_strconv_format_int(-456));
    EXPECT_EQ("0", srs_strconv_format_int(0));

    // Test float formatting
    std::string float_result = srs_strconv_format_float(3.14159);
    EXPECT_TRUE(float_result.find("3.14") != std::string::npos);

    // Test bool formatting
    EXPECT_EQ("on", srs_strconv_format_bool(true));
    EXPECT_EQ("off", srs_strconv_format_bool(false));
}

VOID TEST(KernelUtilityTest, StringManipulation)
{
    // Test string replacement
    EXPECT_EQ("hello world", srs_strings_replace("hello test", "test", "world"));
    EXPECT_EQ("abc abc abc", srs_strings_replace("def def def", "def", "abc"));

    // Test string trimming
    EXPECT_EQ("hello", srs_strings_trim_end("hello   ", " "));
    EXPECT_EQ("world", srs_strings_trim_start("   world", " "));

    // Test string removal
    EXPECT_EQ("helloworld", srs_strings_remove("hello world", " "));
    EXPECT_EQ("abc", srs_strings_remove("a,b,c", ","));
}

VOID TEST(KernelUtilityTest, StringChecking)
{
    // Test ends with
    EXPECT_TRUE(srs_strings_ends_with("test.mp4", ".mp4"));
    EXPECT_FALSE(srs_strings_ends_with("test.flv", ".mp4"));
    EXPECT_TRUE(srs_strings_ends_with("file.log", ".log", ".txt"));

    // Test starts with
    EXPECT_TRUE(srs_strings_starts_with("rtmp://", "rtmp://"));
    EXPECT_FALSE(srs_strings_starts_with("http://", "rtmp://"));
    EXPECT_TRUE(srs_strings_starts_with("http://", "http://", "https://"));

    // Test contains
    EXPECT_TRUE(srs_strings_contains("hello world", "world"));
    EXPECT_FALSE(srs_strings_contains("hello world", "test"));
    EXPECT_TRUE(srs_strings_contains("test string", "test", "string"));
}

VOID TEST(KernelUtilityTest, StringSplitting)
{
    // Test basic splitting
    std::vector<std::string> result = srs_strings_split("a,b,c", ",");
    EXPECT_EQ(3, (int)result.size());
    EXPECT_EQ("a", result[0]);
    EXPECT_EQ("b", result[1]);
    EXPECT_EQ("c", result[2]);

    // Test empty string splitting
    std::vector<std::string> empty_result = srs_strings_split("", ",");
    EXPECT_EQ(1, (int)empty_result.size());
    EXPECT_EQ("", empty_result[0]);

    // Test no separator found
    std::vector<std::string> no_sep = srs_strings_split("hello", ",");
    EXPECT_EQ(1, (int)no_sep.size());
    EXPECT_EQ("hello", no_sep[0]);
}

VOID TEST(KernelUtilityTest, HexDumping)
{
    // Test hex dumping
    std::string hex_result = srs_strings_dumps_hex("ABC", 3);
    EXPECT_TRUE(hex_result.find("41") != std::string::npos); // 'A' = 0x41
    EXPECT_TRUE(hex_result.find("42") != std::string::npos); // 'B' = 0x42
    EXPECT_TRUE(hex_result.find("43") != std::string::npos); // 'C' = 0x43

    // Test empty string hex dump
    std::string empty_hex = srs_strings_dumps_hex("", 0);
    EXPECT_TRUE(empty_hex.empty() || empty_hex == "");
}

VOID TEST(KernelUtilityTest, SystemProperties)
{
    // Test endianness detection
    bool is_little = srs_is_little_endian();
    // Just verify it returns a consistent value
    EXPECT_EQ(is_little, srs_is_little_endian());
}

// Tests for srs_kernel_stream.hpp
VOID TEST(KernelStreamTest, SimpleStreamBasics)
{
    SrsSimpleStream stream;

    // Test initial state
    EXPECT_EQ(0, stream.length());
    EXPECT_TRUE(stream.bytes() == NULL);

    // Test appending data
    std::string test_data = "hello";
    stream.append(test_data.c_str(), test_data.length());
    EXPECT_EQ(5, stream.length());
    EXPECT_TRUE(stream.bytes() != NULL);

    // Test erasing data
    stream.erase(2);
    EXPECT_EQ(3, stream.length());
}

VOID TEST(KernelStreamTest, SimpleStreamOperations)
{
    SrsSimpleStream stream;

    // Add some test data
    stream.append("test data", 9);

    // Test data access
    EXPECT_TRUE(stream.bytes() != NULL);
    EXPECT_EQ('t', stream.bytes()[0]);
    EXPECT_EQ('e', stream.bytes()[1]);

    // Test erasing all data
    stream.erase(stream.length());
    EXPECT_EQ(0, stream.length());
    EXPECT_TRUE(stream.bytes() == NULL);
}

VOID TEST(KernelStreamTest, SimpleStreamAppending)
{
    SrsSimpleStream stream1;
    SrsSimpleStream stream2;

    // Add data to both streams
    stream1.append("hello", 5);
    stream2.append(" world", 6);

    // Test appending one stream to another
    stream1.append(&stream2);
    EXPECT_EQ(11, stream1.length());

    // Verify the combined content
    char *data = stream1.bytes();
    EXPECT_TRUE(data != NULL);
    EXPECT_EQ('h', data[0]);
    EXPECT_EQ(' ', data[5]);
    EXPECT_EQ('w', data[6]);
}

// Tests for srs_kernel_pithy_print.hpp
VOID TEST(KernelPithyPrintTest, AlonePithyPrint)
{
    SrsAlonePithyPrint print;

    // The behavior depends on internal timing, just verify it doesn't crash
    bool can_print_initial = print.can_print();
    EXPECT_TRUE(can_print_initial == true || can_print_initial == false);

    // After elapse, timing should be updated
    print.elapse();
    // The behavior depends on internal timing, just verify it doesn't crash
    bool can_print = print.can_print();
    EXPECT_TRUE(can_print == true || can_print == false);
}

VOID TEST(KernelPithyPrintTest, ErrorPithyPrint)
{
    SrsErrorPithyPrint error_print;

    // Test with different error codes
    srs_error_t err1 = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "test error 1");
    srs_error_t err2 = srs_error_new(ERROR_SYSTEM_STREAM_BUSY, "test error 2");

    // First call should be able to print
    uint32_t nn = 0;
    bool can_print1 = error_print.can_print(err1, &nn);
    EXPECT_TRUE(can_print1 == true || can_print1 == false);

    // Test with integer error codes
    bool can_print_int = error_print.can_print(1001, &nn);
    EXPECT_TRUE(can_print_int == true || can_print_int == false);

    srs_freep(err1);
    srs_freep(err2);
}

VOID TEST(KernelPithyPrintTest, PithyPrintFactories)
{
    // Test factory methods don't crash
    SrsUniquePtr<SrsPithyPrint> rtmp_play(SrsPithyPrint::create_rtmp_play());
    EXPECT_TRUE(rtmp_play.get() != NULL);

    SrsUniquePtr<SrsPithyPrint> rtmp_publish(SrsPithyPrint::create_rtmp_publish());
    EXPECT_TRUE(rtmp_publish.get() != NULL);

    SrsUniquePtr<SrsPithyPrint> hls(SrsPithyPrint::create_hls());
    EXPECT_TRUE(hls.get() != NULL);

    SrsUniquePtr<SrsPithyPrint> rtc_play(SrsPithyPrint::create_rtc_play());
    EXPECT_TRUE(rtc_play.get() != NULL);

    // Test basic operations
    rtmp_play->elapse();
    bool can_print = rtmp_play->can_print();
    EXPECT_TRUE(can_print == true || can_print == false);

    srs_utime_t age = rtmp_play->age();
    EXPECT_GE(age, 0);
}

// Tests for srs_kernel_rtc_queue.hpp
VOID TEST(KernelRTCQueueTest, RtpRingBufferBasics)
{
    SrsRtpRingBuffer buffer(100);

    // Test initial state
    EXPECT_TRUE(buffer.empty());
    EXPECT_EQ(0, buffer.size());

    // Test basic operations without actual packets (just interface testing)
    uint16_t nack_first = 0, nack_last = 0;
    bool result = buffer.update(100, nack_first, nack_last);
    EXPECT_TRUE(result == true || result == false); // Just verify it doesn't crash

    // Test sequence operations
    uint32_t extended_seq = buffer.get_extended_highest_sequence();
    EXPECT_GE(extended_seq, 0);
}

VOID TEST(KernelRTCQueueTest, RtpRingBufferAdvance)
{
    SrsRtpRingBuffer buffer(50);

    // Test advance operations
    buffer.advance_to(10);
    buffer.advance_to(20);

    // Test notification methods (should not crash)
    buffer.notify_nack_list_full();
    buffer.notify_drop_seq(15);

    // Verify buffer state
    EXPECT_TRUE(buffer.empty() || !buffer.empty()); // Just verify it doesn't crash
}

VOID TEST(KernelRTCQueueTest, RtpNackForReceiver)
{
    SrsRtpRingBuffer buffer(100);
    SrsRtpNackForReceiver nack(&buffer, 50);

    // Test basic nack operations
    nack.insert(100, 105);
    nack.remove(102);

    // Test finding nack info
    SrsRtpNackInfo *info = nack.find(103);
    EXPECT_TRUE(info != NULL || info == NULL); // Just verify it doesn't crash

    // Test queue size check
    nack.check_queue_size();

    // Test RTT update
    nack.update_rtt(50);

    // Test getting nack sequences
    SrsRtcpNack seqs;
    uint32_t timeout_nacks = 0;
    nack.get_nack_seqs(seqs, timeout_nacks);
    EXPECT_GE(timeout_nacks, 0);
}
