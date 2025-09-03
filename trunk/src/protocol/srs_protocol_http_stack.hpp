//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_PROTOCOL_HTTP_HPP
#define SRS_PROTOCOL_HTTP_HPP

#include <srs_core.hpp>

#include <srs_kernel_io.hpp>

#include <map>
#include <string>
#include <vector>

// For srs-librtmp, @see https://github.com/ossrs/srs/issues/213
#ifndef _WIN32
#include <sys/uio.h>
#endif

class SrsFileReader;
class SrsHttpHeader;
class ISrsHttpMessage;
class SrsHttpMuxEntry;
class ISrsHttpResponseWriter;
class SrsJsonObject;
class ISrsFileReaderFactory;

// From http specification
// CR             = <US-ASCII CR, carriage return (13)>
#define SRS_HTTP_CR SRS_CONSTS_CR // 0x0D
// LF             = <US-ASCII LF, linefeed (10)>
#define SRS_HTTP_LF SRS_CONSTS_LF // 0x0A
// SP             = <US-ASCII SP, space (32)>
#define SRS_HTTP_SP ' ' // 0x20
// HT             = <US-ASCII HT, horizontal-tab (9)>
#define SRS_HTTP_HT '\x09' // 0x09

// HTTP/1.1 defines the sequence CR LF as the end-of-line marker for all
// protocol elements except the entity-body (see appendix 19.3 for
// tolerant applications).
#define SRS_HTTP_CRLF "\r\n"         // 0x0D0A
#define SRS_HTTP_CRLFCRLF "\r\n\r\n" // 0x0D0A0D0A

// For http parser macros
#define SRS_CONSTS_HTTP_OPTIONS HTTP_OPTIONS
#define SRS_CONSTS_HTTP_GET HTTP_GET
#define SRS_CONSTS_HTTP_POST HTTP_POST
#define SRS_CONSTS_HTTP_PUT HTTP_PUT
#define SRS_CONSTS_HTTP_DELETE HTTP_DELETE

// Error replies to the request with the specified error message and HTTP code.
// The error message should be plain text.
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code);
extern srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code, std::string error);

// Get the status text of code.
extern std::string srs_generate_http_status_text(int status);

// It reports whether a given response status code
// permits a body.  See RFC2616, section 4.4.
extern bool srs_go_http_body_allowd(int status);

// DetectContentType implements the algorithm described
// at http://mimesniff.spec.whatwg.org/ to determine the
// Content-Type of the given data.  It considers at most the
// first 512 bytes of data.  DetectContentType always returns
// a valid MIME type: if it cannot determine a more specific one, it
// returns "application/octet-stream".
extern std::string srs_go_http_detect(char *data, int size);

// The state of HTTP message
enum SrsHttpParseState {
    SrsHttpParseStateInit = 0,
    SrsHttpParseStateStart,
    SrsHttpParseStateHeaderComplete,
    SrsHttpParseStateBody,
    SrsHttpParseStateMessageComplete
};

// A Header represents the key-value pairs in an HTTP header.
class SrsHttpHeader
{
private:
    // The order in which header fields with differing field names are
    // received is not significant. However, it is "good practice" to send
    // general-header fields first, followed by request-header or response-
    // header fields, and ending with the entity-header fields.
    // @doc https://tools.ietf.org/html/rfc2616#section-4.2
    std::map<std::string, std::string> headers;
    // Store keys to keep fields in order.
    std::vector<std::string> keys_;

public:
    SrsHttpHeader();
    virtual ~SrsHttpHeader();

public:
    // Add adds the key, value pair to the header.
    // It appends to any existing values associated with key.
    virtual void set(std::string key, std::string value);
    // Get gets the first value associated with the given key.
    // If there are no values associated with the key, Get returns "".
    // To access multiple values of a key, access the map directly
    // with CanonicalHeaderKey.
    virtual std::string get(std::string key);
    // Delete the http header indicated by key.
    // Return the removed header field.
    virtual void del(std::string);
    // Get the count of headers.
    virtual int count();

public:
    // Dumps to a JSON object.
    virtual void dumps(SrsJsonObject *o);

public:
    // Get the content length. -1 if not set.
    virtual int64_t content_length();
    // set the content length by header "Content-Length"
    virtual void set_content_length(int64_t size);

public:
    // Get the content type. empty string if not set.
    virtual std::string content_type();
    // set the content type by header "Content-Type"
    virtual void set_content_type(std::string ct);

public:
    // write all headers to string stream.
    virtual void write(std::stringstream &ss);
};

// A ResponseWriter interface is used by an HTTP handler to
// construct an HTTP response.
// Usage 0, response with a message once:
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->write((char*)msg.data(), (int)msg.length());
// Usage 1, response with specified length content, same to #0:
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("text/plain; charset=utf-8");
//      w->header()->set_content_length(msg.length());
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->write((char*)msg.data(), (int)msg.length()); // write N times, N>0
//      w->final_request(); // optional flush.
// Usage 2, response with HTTP code only, zero content length.
//      ISrsHttpResponseWriter* w; // create or get response.
//      w->header()->set_content_length(0);
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->final_request();
// Usage 3, response in chunked encoding.
//      ISrsHttpResponseWriter* w; // create or get response.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("application/octet-stream");
//      w->write_header(SRS_CONSTS_HTTP_OK);
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->final_request(); // required to end the chunked and flush.
class ISrsHttpResponseWriter
{
public:
    ISrsHttpResponseWriter();
    virtual ~ISrsHttpResponseWriter();

public:
    // When chunked mode,
    // final the request to complete the chunked encoding.
    // For no-chunked mode,
    // final to send request, for example, content-length is 0.
    virtual srs_error_t final_request() = 0;

    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsHttpHeader *header() = 0;

    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char *data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

    // WriteHeader sends an HTTP response header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
    virtual void write_header(int code) = 0;
};

// The reader interface for http response.
class ISrsHttpResponseReader : public ISrsReader
{
public:
    ISrsHttpResponseReader();
    virtual ~ISrsHttpResponseReader();

public:
    // Whether response read EOF.
    virtual bool eof() = 0;
};

// A RequestWriter interface is used by an HTTP handler to
// construct an HTTP request.
// Usage 0, request with a message once:
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->write((char*)msg.data(), (int)msg.length());
// Usage 1, request with specified length content, same to #0:
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("text/plain; charset=utf-8");
//      w->header()->set_content_length(msg.length());
//      w->write_header("POST", "/");
//      w->write((char*)msg.data(), (int)msg.length()); // write N times, N>0
//      w->final_request(); // optional flush.
// Usage 2, request with HTTP code only, zero content length.
//      ISrsHttpRequestWriter* w; // create or get request.
//      w->header()->set_content_length(0);
//      w->write_header("GET", "/");
//      w->final_request();
// Usage 3, request in chunked encoding.
//      ISrsHttpRequestWriter* w; // create or get request.
//      std::string msg = "Hello, HTTP!";
//      w->header()->set_content_type("application/octet-stream");
//      w->write_header("POST", "/");
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->write((char*)msg.data(), (int)msg.length());
//      w->final_request(); // required to end the chunked and flush.
class ISrsHttpRequestWriter
{
public:
    ISrsHttpRequestWriter();
    virtual ~ISrsHttpRequestWriter();

public:
    // When chunked mode,
    // final the request to complete the chunked encoding.
    // For no-chunked mode,
    // final to send request, for example, content-length is 0.
    virtual srs_error_t final_request() = 0;

    // Header returns the header map that will be sent by WriteHeader.
    // Changing the header after a call to WriteHeader (or Write) has
    // no effect.
    virtual SrsHttpHeader *header() = 0;

    // Write writes the data to the connection as part of an HTTP reply.
    // If WriteHeader has not yet been called, Write calls WriteHeader(http.StatusOK)
    // before writing the data.  If the Header does not contain a
    // Content-Type line, Write adds a Content-Type set to the result of passing
    // The initial 512 bytes of written data to DetectContentType.
    // @param data, the data to send. NULL to flush header only.
    virtual srs_error_t write(char *data, int size) = 0;
    // for the HTTP FLV, to writev to improve performance.
    virtual srs_error_t writev(const iovec *iov, int iovcnt, ssize_t *pnwrite) = 0;

    // WriteHeader sends an HTTP request header with status code.
    // If WriteHeader is not called explicitly, the first call to Write
    // will trigger an implicit WriteHeader(http.StatusOK).
    // Thus explicit calls to WriteHeader are mainly used to
    // send error codes.
    // @remark, user must set header then write or write_header.
    virtual void write_header(const std::string &method, const std::string &path) = 0;
};

// Objects implementing the Handler interface can be
// registered to serve a particular path or subtree
// in the HTTP server.
//
// ServeHTTP should write reply headers and data to the ResponseWriter
// and then return.  Returning signals that the request is finished
// and that the HTTP server can move on to the next request on
// The connection.
class ISrsHttpHandler
{
public:
    SrsHttpMuxEntry *entry;

public:
    ISrsHttpHandler();
    virtual ~ISrsHttpHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r) = 0;
};

// Redirect to a fixed URL
class SrsHttpRedirectHandler : public ISrsHttpHandler
{
private:
    std::string url;
    int code;

public:
    SrsHttpRedirectHandler(std::string u, int c);
    virtual ~SrsHttpRedirectHandler();

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// NotFound replies to the request with an HTTP 404 not found error.
class SrsHttpNotFoundHandler : public ISrsHttpHandler
{
public:
    SrsHttpNotFoundHandler();
    virtual ~SrsHttpNotFoundHandler();

public:
    virtual bool is_not_found();
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// For utest to mock it.
typedef bool (*_pfn_srs_path_exists)(std::string path);

// Build the file path from request r.
extern std::string srs_http_fs_fullpath(std::string dir, std::string pattern, std::string upath);

// FileServer returns a handler that serves HTTP requests
// with the contents of the file system rooted at root.
//
// To use the operating system's file system implementation,
// use http.Dir:
//
//     http.Handle("/", SrsHttpFileServer("/tmp"))
//     http.Handle("/", SrsHttpFileServer("static-dir"))
class SrsHttpFileServer : public ISrsHttpHandler
{
protected:
    std::string dir;

protected:
    ISrsFileReaderFactory *fs_factory;
    _pfn_srs_path_exists _srs_path_exists;

public:
    SrsHttpFileServer(std::string root_dir);
    virtual ~SrsHttpFileServer();

private:
    // For utest to mock the fs.
    virtual void set_fs_factory(ISrsFileReaderFactory *v);
    // For utest to mock the path check function.
    virtual void set_path_check(_pfn_srs_path_exists pfn);

public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

private:
    // Serve the file by specified path
    virtual srs_error_t serve_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_flv_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    virtual srs_error_t serve_mp4_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

protected:
    // When access flv file with x.flv?start=xxx
    virtual srs_error_t serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t offset);
    // When access mp4 file with x.mp4?range=start-end
    // @param start the start offset in bytes.
    // @param end the end offset in bytes. -1 to end of file.
    // @remark response data in [start, end].
    virtual srs_error_t serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath, int64_t start, int64_t end);
    // For HLS protocol.
    // When the request url, like as "http://127.0.0.1:8080/live/livestream.m3u8",
    // returns the response like as "http://127.0.0.1:8080/live/livestream.m3u8?hls_ctx=12345678" .
    // SRS use "hls_ctx" to keep track of subsequent requests that is short-connection.
    // Remark 1:
    //           Fill the parameter "hls_ctx" by yourself in the first request is allowed, SRS will use it.
    //           And MUST make sure it is unique.
    // Remark 2:
    //           If use two same "hls_ctx" in different requests, SRS cannot detect so that they will be treated as one.
    virtual srs_error_t serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);
    // the ts file including: .ts .m4s init.mp4
    virtual srs_error_t serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath);

protected:
    // Copy the fs to response writer in size bytes.
    virtual srs_error_t copy(ISrsHttpResponseWriter *w, SrsFileReader *fs, ISrsHttpMessage *r, int64_t size);
};

// The mux entry for server mux.
// The matcher info, for example, the pattern and handler.
class SrsHttpMuxEntry
{
public:
    bool explicit_match;
    ISrsHttpHandler *handler;
    std::string pattern;
    bool enabled;

public:
    SrsHttpMuxEntry();
    virtual ~SrsHttpMuxEntry();
};

// The dynamic matcher for http pattern match.
class ISrsHttpDynamicMatcher
{
public:
    ISrsHttpDynamicMatcher();
    virtual ~ISrsHttpDynamicMatcher();

public:
    // When match the request failed, no handler to process request.
    // @param request the http request message to match the handler.
    // @param ph the already matched handler, dynamic matcher can rewrite it.
    virtual srs_error_t dynamic_match(ISrsHttpMessage *request, ISrsHttpHandler **ph) = 0;
};

// The server mux, all http server should implements it.
class ISrsHttpServeMux : public ISrsHttpHandler
{
public:
    ISrsHttpServeMux();
    virtual ~ISrsHttpServeMux();

public:
    // Register HTTP handler to mux.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler) = 0;
};

// ServeMux is an HTTP request multiplexer.
// It matches the URL of each incoming request against a list of registered
// patterns and calls the handler for the pattern that
// most closely matches the URL.
//
// Patterns name fixed, rooted paths, like "/favicon.ico",
// or rooted subtrees, like "/images/" (note the trailing slash).
// Longer patterns take precedence over shorter ones, so that
// if there are handlers registered for both "/images/"
// and "/images/thumbnails/", the latter handler will be
// called for paths beginning "/images/thumbnails/" and the
// former will receive requests for any other paths in the
// "/images/" subtree.
//
// Note that since a pattern ending in a slash names a rooted subtree,
// The pattern "/" matches all paths not matched by other registered
// patterns, not just the URL with Path == "/".
//
// Patterns may optionally begin with a host name, restricting matches to
// URLs on that host only.  Host-specific patterns take precedence over
// general patterns, so that a handler might register for the two patterns
// "/codesearch" and "codesearch.google.com/" without also taking over
// requests for "http://www.google.com/".
//
// ServeMux also takes care of sanitizing the URL request path,
// redirecting any request containing . or .. elements to an
// equivalent .- and ..-free URL.
class SrsHttpServeMux : public ISrsHttpServeMux
{
private:
    // The pattern handler, to handle the http request.
    std::map<std::string, SrsHttpMuxEntry *> static_matchers_;
    // The vhost handler.
    // When find the handler to process the request,
    // append the matched vhost when pattern not starts with /,
    // For example, for pattern /live/livestream.flv of vhost ossrs.net,
    // The path will rewrite to ossrs.net/live/livestream.flv
    std::map<std::string, ISrsHttpHandler *> vhosts_;

private:
    // all dynamic matcher for http match.
    // For example, the hstrs(http stream trigger rtmp source)
    // can dynamic match and install handler when request incoming and no handler.
    std::vector<ISrsHttpDynamicMatcher *> dynamic_matchers_;

public:
    SrsHttpServeMux();
    virtual ~SrsHttpServeMux();

public:
    // Initialize the http serve mux.
    virtual srs_error_t initialize();

public:
    // Add a dynamic matcher for the http match.
    virtual void add_dynamic_matcher(ISrsHttpDynamicMatcher *h);
    virtual void remove_dynamic_matcher(ISrsHttpDynamicMatcher *h);

public:
    // Handle registers the handler for the given pattern.
    // If a handler already exists for pattern, Handle panics.
    virtual srs_error_t handle(std::string pattern, ISrsHttpHandler *handler);
    // Remove the handler for pattern. Note that this will not free the handler.
    void unhandle(std::string pattern, ISrsHttpHandler *handler);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

public:
    virtual srs_error_t find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph);

private:
    virtual srs_error_t match(ISrsHttpMessage *r, ISrsHttpHandler **ph);
    virtual bool path_match(std::string pattern, std::string path);
};

// The filter http mux, directly serve the http CORS requests
class SrsHttpCorsMux : public ISrsHttpHandler
{
private:
    bool required;
    bool enabled;
    ISrsHttpHandler *next_;

public:
    SrsHttpCorsMux(ISrsHttpHandler *h);
    virtual ~SrsHttpCorsMux();

public:
    virtual srs_error_t initialize(bool cros_enabled);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// The filter http mux, directly serve the http AUTH requests,
// while proxy to the worker mux for services.
// @see https://www.rfc-editor.org/rfc/rfc7617
// @see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/WWW-Authenticate
class SrsHttpAuthMux : public ISrsHttpHandler
{
private:
    bool enabled_;
    std::string username_;
    std::string password_;
    ISrsHttpHandler *next_;

public:
    SrsHttpAuthMux(ISrsHttpHandler *h);
    virtual ~SrsHttpAuthMux();

public:
    virtual srs_error_t initialize(bool enabled, std::string username, std::string password);
    // Interface ISrsHttpServeMux
public:
    virtual srs_error_t serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);

private:
    virtual srs_error_t do_auth(ISrsHttpResponseWriter *w, ISrsHttpMessage *r);
};

// A Request represents an HTTP request received by a server
// or to be sent by a client.
//
// The field semantics differ slightly between client and server
// usage. In addition to the notes on the fields below, see the
// documentation for Request.Write and RoundTripper.
//
// There are some modes to determine the length of body:
//      1. content-length and chunked.
//      2. infinite chunked.
//      3. no body.
// For example:
//      ISrsHttpMessage* r = ...;
//      while (!r->eof()) r->read(); // Read in mode 1 or 3.
// @rmark for mode 2, the infinite chunked, all left data is body.
class ISrsHttpMessage
{
public:
    ISrsHttpMessage();
    virtual ~ISrsHttpMessage();

public:
    virtual uint8_t message_type() = 0;
    virtual uint8_t method() = 0;
    virtual uint16_t status_code() = 0;
    // Method helpers.
    virtual std::string method_str() = 0;
    virtual bool is_http_get() = 0;
    virtual bool is_http_put() = 0;
    virtual bool is_http_post() = 0;
    virtual bool is_http_delete() = 0;
    virtual bool is_http_options() = 0;

public:
    // Whether should keep the connection alive.
    virtual bool is_keep_alive() = 0;
    // The uri contains the host and path.
    virtual std::string uri() = 0;
    // The url maybe the path.
    virtual std::string url() = 0;
    virtual std::string host() = 0;
    virtual std::string path() = 0;
    virtual std::string query() = 0;
    virtual std::string ext() = 0;
    // Get the RESTful id, in string,
    // for example, pattern is /api/v1/streams, path is /api/v1/streams/100,
    // then the rest id is 100.
    // @param pattern the handler pattern which will serve the request.
    // @return the REST id; "" if not matched.
    virtual std::string parse_rest_id(std::string pattern) = 0;

public:
    // Read body to string.
    // @remark for small http body.
    virtual srs_error_t body_read_all(std::string &body) = 0;
    // Get the body reader, to read one by one.
    // @remark when body is very large, or chunked, use this.
    virtual ISrsHttpResponseReader *body_reader() = 0;
    // The content length, -1 for chunked or not set.
    virtual int64_t content_length() = 0;

public:
    // Get the param in query string,
    // for instance, query is "start=100&end=200",
    // then query_get("start") is "100", and query_get("end") is "200"
    virtual std::string query_get(std::string key) = 0;
    // Get the headers.
    virtual SrsHttpHeader *header() = 0;

public:
    // Whether the current request is JSONP,
    // which has a "callback=xxx" in QueryString.
    virtual bool is_jsonp() = 0;
};

// Used to resolve the http uri.
class SrsHttpUri
{
private:
    std::string url_;
    std::string schema;
    std::string host;
    int port;
    std::string path;
    std::string query;
    std::string fragment_;
    std::string username_;
    std::string password_;
    std::map<std::string, std::string> query_values_;

public:
    SrsHttpUri();
    virtual ~SrsHttpUri();

public:
    // Initialize the http uri.
    virtual srs_error_t initialize(std::string _url);
    // After parsed the message, set the schema to https.
    virtual void set_schema(std::string v);

public:
    virtual std::string get_url();
    virtual std::string get_schema();
    virtual std::string get_host();
    virtual int get_port();
    virtual std::string get_path();
    virtual std::string get_query();
    virtual std::string get_query_by_key(std::string key);
    virtual std::string get_fragment();
    virtual std::string username();
    virtual std::string password();

private:
    // Simple URL parser to replace http-parser URL parsing
    virtual srs_error_t parse_url_simple(const std::string &url, std::string &schema, std::string &host, int &port,
                                         std::string &path, std::string &query, std::string &fragment,
                                         std::string &username, std::string &password);
    srs_error_t parse_query();

public:
    static std::string query_escape(std::string s);
    static std::string path_escape(std::string s);
    static srs_error_t query_unescape(std::string s, std::string &value);
    static srs_error_t path_unescape(std::string s, std::string &value);
};

// Decode a base64-encoded string.
extern srs_error_t srs_av_base64_decode(std::string cipher, std::string &plaintext);
// Encode a plaintext to  base64-encoded string.
extern srs_error_t srs_av_base64_encode(std::string plaintext, std::string &cipher);

// For #ifndef SRS_PROTOCOL_HTTP_HPP
#endif

// The llhttp is licensed under MIT, see https://github.com/nodejs/llhttp
// Version:
//      9.3.0
// The files are generated by:
//      git clone https://github.com/nodejs/llhttp.git
//      cd llhttp
//      make
// Note that these files are copied to SRS and formated by clang-format:
//      ./scripts/clang_format.sh
// Copy the header files to bellow:
//      build/llhttp.h

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

#ifndef INCLUDE_LLHTTP_H_
#define INCLUDE_LLHTTP_H_

#define LLHTTP_VERSION_MAJOR 9
#define LLHTTP_VERSION_MINOR 3
#define LLHTTP_VERSION_PATCH 0

#ifndef INCLUDE_LLHTTP_ITSELF_H_
#define INCLUDE_LLHTTP_ITSELF_H_
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct llhttp__internal_s llhttp__internal_t;
struct llhttp__internal_s {
    int32_t _index;
    void *_span_pos0;
    void *_span_cb0;
    int32_t error;
    const char *reason;
    const char *error_pos;
    void *data;
    void *_current;
    uint64_t content_length;
    uint8_t type;
    uint8_t method;
    uint8_t http_major;
    uint8_t http_minor;
    uint8_t header_state;
    uint16_t lenient_flags;
    uint8_t upgrade;
    uint8_t finish;
    uint16_t flags;
    uint16_t status_code;
    uint8_t initial_message_completed;
    void *settings;
};

int llhttp__internal_init(llhttp__internal_t *s);
int llhttp__internal_execute(llhttp__internal_t *s, const char *p, const char *endp);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* INCLUDE_LLHTTP_ITSELF_H_ */

#ifndef LLLLHTTP_C_HEADERS_
#define LLLLHTTP_C_HEADERS_
#ifdef __cplusplus
extern "C" {
#endif

enum llhttp_errno {
    HPE_OK = 0,
    HPE_INTERNAL = 1,
    HPE_STRICT = 2,
    HPE_LF_EXPECTED = 3,
    HPE_UNEXPECTED_CONTENT_LENGTH = 4,
    HPE_CLOSED_CONNECTION = 5,
    HPE_INVALID_METHOD = 6,
    HPE_INVALID_URL = 7,
    HPE_INVALID_CONSTANT = 8,
    HPE_INVALID_VERSION = 9,
    HPE_INVALID_HEADER_TOKEN = 10,
    HPE_INVALID_CONTENT_LENGTH = 11,
    HPE_INVALID_CHUNK_SIZE = 12,
    HPE_INVALID_STATUS = 13,
    HPE_INVALID_EOF_STATE = 14,
    HPE_INVALID_TRANSFER_ENCODING = 15,
    HPE_CB_MESSAGE_BEGIN = 16,
    HPE_CB_HEADERS_COMPLETE = 17,
    HPE_CB_MESSAGE_COMPLETE = 18,
    HPE_CB_CHUNK_HEADER = 19,
    HPE_CB_CHUNK_COMPLETE = 20,
    HPE_PAUSED = 21,
    HPE_PAUSED_UPGRADE = 22,
    HPE_PAUSED_H2_UPGRADE = 23,
    HPE_USER = 24,
    HPE_CR_EXPECTED = 25,
    HPE_CB_URL_COMPLETE = 26,
    HPE_CB_STATUS_COMPLETE = 27,
    HPE_CB_HEADER_FIELD_COMPLETE = 28,
    HPE_CB_HEADER_VALUE_COMPLETE = 29,
    HPE_UNEXPECTED_SPACE = 30,
    HPE_CB_RESET = 31,
    HPE_CB_METHOD_COMPLETE = 32,
    HPE_CB_VERSION_COMPLETE = 33,
    HPE_CB_CHUNK_EXTENSION_NAME_COMPLETE = 34,
    HPE_CB_CHUNK_EXTENSION_VALUE_COMPLETE = 35,
    HPE_CB_PROTOCOL_COMPLETE = 38
};
typedef enum llhttp_errno llhttp_errno_t;

enum llhttp_flags {
    F_CONNECTION_KEEP_ALIVE = 0x1,
    F_CONNECTION_CLOSE = 0x2,
    F_CONNECTION_UPGRADE = 0x4,
    F_CHUNKED = 0x8,
    F_UPGRADE = 0x10,
    F_CONTENT_LENGTH = 0x20,
    F_SKIPBODY = 0x40,
    F_TRAILING = 0x80,
    F_TRANSFER_ENCODING = 0x200
};
typedef enum llhttp_flags llhttp_flags_t;

enum llhttp_lenient_flags {
    LENIENT_HEADERS = 0x1,
    LENIENT_CHUNKED_LENGTH = 0x2,
    LENIENT_KEEP_ALIVE = 0x4,
    LENIENT_TRANSFER_ENCODING = 0x8,
    LENIENT_VERSION = 0x10,
    LENIENT_DATA_AFTER_CLOSE = 0x20,
    LENIENT_OPTIONAL_LF_AFTER_CR = 0x40,
    LENIENT_OPTIONAL_CRLF_AFTER_CHUNK = 0x80,
    LENIENT_OPTIONAL_CR_BEFORE_LF = 0x100,
    LENIENT_SPACES_AFTER_CHUNK_SIZE = 0x200
};
typedef enum llhttp_lenient_flags llhttp_lenient_flags_t;

enum llhttp_type {
    HTTP_BOTH = 0,
    HTTP_REQUEST = 1,
    HTTP_RESPONSE = 2
};
typedef enum llhttp_type llhttp_type_t;

enum llhttp_finish {
    HTTP_FINISH_SAFE = 0,
    HTTP_FINISH_SAFE_WITH_CB = 1,
    HTTP_FINISH_UNSAFE = 2
};
typedef enum llhttp_finish llhttp_finish_t;

enum llhttp_method {
    HTTP_DELETE = 0,
    HTTP_GET = 1,
    HTTP_HEAD = 2,
    HTTP_POST = 3,
    HTTP_PUT = 4,
    HTTP_CONNECT = 5,
    HTTP_OPTIONS = 6,
    HTTP_TRACE = 7,
    HTTP_COPY = 8,
    HTTP_LOCK = 9,
    HTTP_MKCOL = 10,
    HTTP_MOVE = 11,
    HTTP_PROPFIND = 12,
    HTTP_PROPPATCH = 13,
    HTTP_SEARCH = 14,
    HTTP_UNLOCK = 15,
    HTTP_BIND = 16,
    HTTP_REBIND = 17,
    HTTP_UNBIND = 18,
    HTTP_ACL = 19,
    HTTP_REPORT = 20,
    HTTP_MKACTIVITY = 21,
    HTTP_CHECKOUT = 22,
    HTTP_MERGE = 23,
    HTTP_MSEARCH = 24,
    HTTP_NOTIFY = 25,
    HTTP_SUBSCRIBE = 26,
    HTTP_UNSUBSCRIBE = 27,
    HTTP_PATCH = 28,
    HTTP_PURGE = 29,
    HTTP_MKCALENDAR = 30,
    HTTP_LINK = 31,
    HTTP_UNLINK = 32,
    HTTP_SOURCE = 33,
    HTTP_PRI = 34,
    HTTP_DESCRIBE = 35,
    HTTP_ANNOUNCE = 36,
    HTTP_SETUP = 37,
    HTTP_PLAY = 38,
    HTTP_PAUSE = 39,
    HTTP_TEARDOWN = 40,
    HTTP_GET_PARAMETER = 41,
    HTTP_SET_PARAMETER = 42,
    HTTP_REDIRECT = 43,
    HTTP_RECORD = 44,
    HTTP_FLUSH = 45,
    HTTP_QUERY = 46
};
typedef enum llhttp_method llhttp_method_t;

enum llhttp_status {
    HTTP_STATUS_CONTINUE = 100,
    HTTP_STATUS_SWITCHING_PROTOCOLS = 101,
    HTTP_STATUS_PROCESSING = 102,
    HTTP_STATUS_EARLY_HINTS = 103,
    HTTP_STATUS_RESPONSE_IS_STALE = 110,
    HTTP_STATUS_REVALIDATION_FAILED = 111,
    HTTP_STATUS_DISCONNECTED_OPERATION = 112,
    HTTP_STATUS_HEURISTIC_EXPIRATION = 113,
    HTTP_STATUS_MISCELLANEOUS_WARNING = 199,
    HTTP_STATUS_OK = 200,
    HTTP_STATUS_CREATED = 201,
    HTTP_STATUS_ACCEPTED = 202,
    HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION = 203,
    HTTP_STATUS_NO_CONTENT = 204,
    HTTP_STATUS_RESET_CONTENT = 205,
    HTTP_STATUS_PARTIAL_CONTENT = 206,
    HTTP_STATUS_MULTI_STATUS = 207,
    HTTP_STATUS_ALREADY_REPORTED = 208,
    HTTP_STATUS_TRANSFORMATION_APPLIED = 214,
    HTTP_STATUS_IM_USED = 226,
    HTTP_STATUS_MISCELLANEOUS_PERSISTENT_WARNING = 299,
    HTTP_STATUS_MULTIPLE_CHOICES = 300,
    HTTP_STATUS_MOVED_PERMANENTLY = 301,
    HTTP_STATUS_FOUND = 302,
    HTTP_STATUS_SEE_OTHER = 303,
    HTTP_STATUS_NOT_MODIFIED = 304,
    HTTP_STATUS_USE_PROXY = 305,
    HTTP_STATUS_SWITCH_PROXY = 306,
    HTTP_STATUS_TEMPORARY_REDIRECT = 307,
    HTTP_STATUS_PERMANENT_REDIRECT = 308,
    HTTP_STATUS_BAD_REQUEST = 400,
    HTTP_STATUS_UNAUTHORIZED = 401,
    HTTP_STATUS_PAYMENT_REQUIRED = 402,
    HTTP_STATUS_FORBIDDEN = 403,
    HTTP_STATUS_NOT_FOUND = 404,
    HTTP_STATUS_METHOD_NOT_ALLOWED = 405,
    HTTP_STATUS_NOT_ACCEPTABLE = 406,
    HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED = 407,
    HTTP_STATUS_REQUEST_TIMEOUT = 408,
    HTTP_STATUS_CONFLICT = 409,
    HTTP_STATUS_GONE = 410,
    HTTP_STATUS_LENGTH_REQUIRED = 411,
    HTTP_STATUS_PRECONDITION_FAILED = 412,
    HTTP_STATUS_PAYLOAD_TOO_LARGE = 413,
    HTTP_STATUS_URI_TOO_LONG = 414,
    HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE = 415,
    HTTP_STATUS_RANGE_NOT_SATISFIABLE = 416,
    HTTP_STATUS_EXPECTATION_FAILED = 417,
    HTTP_STATUS_IM_A_TEAPOT = 418,
    HTTP_STATUS_PAGE_EXPIRED = 419,
    HTTP_STATUS_ENHANCE_YOUR_CALM = 420,
    HTTP_STATUS_MISDIRECTED_REQUEST = 421,
    HTTP_STATUS_UNPROCESSABLE_ENTITY = 422,
    HTTP_STATUS_LOCKED = 423,
    HTTP_STATUS_FAILED_DEPENDENCY = 424,
    HTTP_STATUS_TOO_EARLY = 425,
    HTTP_STATUS_UPGRADE_REQUIRED = 426,
    HTTP_STATUS_PRECONDITION_REQUIRED = 428,
    HTTP_STATUS_TOO_MANY_REQUESTS = 429,
    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE_UNOFFICIAL = 430,
    HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431,
    HTTP_STATUS_LOGIN_TIMEOUT = 440,
    HTTP_STATUS_NO_RESPONSE = 444,
    HTTP_STATUS_RETRY_WITH = 449,
    HTTP_STATUS_BLOCKED_BY_PARENTAL_CONTROL = 450,
    HTTP_STATUS_UNAVAILABLE_FOR_LEGAL_REASONS = 451,
    HTTP_STATUS_CLIENT_CLOSED_LOAD_BALANCED_REQUEST = 460,
    HTTP_STATUS_INVALID_X_FORWARDED_FOR = 463,
    HTTP_STATUS_REQUEST_HEADER_TOO_LARGE = 494,
    HTTP_STATUS_SSL_CERTIFICATE_ERROR = 495,
    HTTP_STATUS_SSL_CERTIFICATE_REQUIRED = 496,
    HTTP_STATUS_HTTP_REQUEST_SENT_TO_HTTPS_PORT = 497,
    HTTP_STATUS_INVALID_TOKEN = 498,
    HTTP_STATUS_CLIENT_CLOSED_REQUEST = 499,
    HTTP_STATUS_INTERNAL_SERVER_ERROR = 500,
    HTTP_STATUS_NOT_IMPLEMENTED = 501,
    HTTP_STATUS_BAD_GATEWAY = 502,
    HTTP_STATUS_SERVICE_UNAVAILABLE = 503,
    HTTP_STATUS_GATEWAY_TIMEOUT = 504,
    HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED = 505,
    HTTP_STATUS_VARIANT_ALSO_NEGOTIATES = 506,
    HTTP_STATUS_INSUFFICIENT_STORAGE = 507,
    HTTP_STATUS_LOOP_DETECTED = 508,
    HTTP_STATUS_BANDWIDTH_LIMIT_EXCEEDED = 509,
    HTTP_STATUS_NOT_EXTENDED = 510,
    HTTP_STATUS_NETWORK_AUTHENTICATION_REQUIRED = 511,
    HTTP_STATUS_WEB_SERVER_UNKNOWN_ERROR = 520,
    HTTP_STATUS_WEB_SERVER_IS_DOWN = 521,
    HTTP_STATUS_CONNECTION_TIMEOUT = 522,
    HTTP_STATUS_ORIGIN_IS_UNREACHABLE = 523,
    HTTP_STATUS_TIMEOUT_OCCURED = 524,
    HTTP_STATUS_SSL_HANDSHAKE_FAILED = 525,
    HTTP_STATUS_INVALID_SSL_CERTIFICATE = 526,
    HTTP_STATUS_RAILGUN_ERROR = 527,
    HTTP_STATUS_SITE_IS_OVERLOADED = 529,
    HTTP_STATUS_SITE_IS_FROZEN = 530,
    HTTP_STATUS_IDENTITY_PROVIDER_AUTHENTICATION_ERROR = 561,
    HTTP_STATUS_NETWORK_READ_TIMEOUT = 598,
    HTTP_STATUS_NETWORK_CONNECT_TIMEOUT = 599
};
typedef enum llhttp_status llhttp_status_t;

#define HTTP_ERRNO_MAP(XX)                                                       \
    XX(0, OK, OK)                                                                \
    XX(1, INTERNAL, INTERNAL)                                                    \
    XX(2, STRICT, STRICT)                                                        \
    XX(3, LF_EXPECTED, LF_EXPECTED)                                              \
    XX(4, UNEXPECTED_CONTENT_LENGTH, UNEXPECTED_CONTENT_LENGTH)                  \
    XX(5, CLOSED_CONNECTION, CLOSED_CONNECTION)                                  \
    XX(6, INVALID_METHOD, INVALID_METHOD)                                        \
    XX(7, INVALID_URL, INVALID_URL)                                              \
    XX(8, INVALID_CONSTANT, INVALID_CONSTANT)                                    \
    XX(9, INVALID_VERSION, INVALID_VERSION)                                      \
    XX(10, INVALID_HEADER_TOKEN, INVALID_HEADER_TOKEN)                           \
    XX(11, INVALID_CONTENT_LENGTH, INVALID_CONTENT_LENGTH)                       \
    XX(12, INVALID_CHUNK_SIZE, INVALID_CHUNK_SIZE)                               \
    XX(13, INVALID_STATUS, INVALID_STATUS)                                       \
    XX(14, INVALID_EOF_STATE, INVALID_EOF_STATE)                                 \
    XX(15, INVALID_TRANSFER_ENCODING, INVALID_TRANSFER_ENCODING)                 \
    XX(16, CB_MESSAGE_BEGIN, CB_MESSAGE_BEGIN)                                   \
    XX(17, CB_HEADERS_COMPLETE, CB_HEADERS_COMPLETE)                             \
    XX(18, CB_MESSAGE_COMPLETE, CB_MESSAGE_COMPLETE)                             \
    XX(19, CB_CHUNK_HEADER, CB_CHUNK_HEADER)                                     \
    XX(20, CB_CHUNK_COMPLETE, CB_CHUNK_COMPLETE)                                 \
    XX(21, PAUSED, PAUSED)                                                       \
    XX(22, PAUSED_UPGRADE, PAUSED_UPGRADE)                                       \
    XX(23, PAUSED_H2_UPGRADE, PAUSED_H2_UPGRADE)                                 \
    XX(24, USER, USER)                                                           \
    XX(25, CR_EXPECTED, CR_EXPECTED)                                             \
    XX(26, CB_URL_COMPLETE, CB_URL_COMPLETE)                                     \
    XX(27, CB_STATUS_COMPLETE, CB_STATUS_COMPLETE)                               \
    XX(28, CB_HEADER_FIELD_COMPLETE, CB_HEADER_FIELD_COMPLETE)                   \
    XX(29, CB_HEADER_VALUE_COMPLETE, CB_HEADER_VALUE_COMPLETE)                   \
    XX(30, UNEXPECTED_SPACE, UNEXPECTED_SPACE)                                   \
    XX(31, CB_RESET, CB_RESET)                                                   \
    XX(32, CB_METHOD_COMPLETE, CB_METHOD_COMPLETE)                               \
    XX(33, CB_VERSION_COMPLETE, CB_VERSION_COMPLETE)                             \
    XX(34, CB_CHUNK_EXTENSION_NAME_COMPLETE, CB_CHUNK_EXTENSION_NAME_COMPLETE)   \
    XX(35, CB_CHUNK_EXTENSION_VALUE_COMPLETE, CB_CHUNK_EXTENSION_VALUE_COMPLETE) \
    XX(38, CB_PROTOCOL_COMPLETE, CB_PROTOCOL_COMPLETE)

#define HTTP_METHOD_MAP(XX)          \
    XX(0, DELETE, DELETE)            \
    XX(1, GET, GET)                  \
    XX(2, HEAD, HEAD)                \
    XX(3, POST, POST)                \
    XX(4, PUT, PUT)                  \
    XX(5, CONNECT, CONNECT)          \
    XX(6, OPTIONS, OPTIONS)          \
    XX(7, TRACE, TRACE)              \
    XX(8, COPY, COPY)                \
    XX(9, LOCK, LOCK)                \
    XX(10, MKCOL, MKCOL)             \
    XX(11, MOVE, MOVE)               \
    XX(12, PROPFIND, PROPFIND)       \
    XX(13, PROPPATCH, PROPPATCH)     \
    XX(14, SEARCH, SEARCH)           \
    XX(15, UNLOCK, UNLOCK)           \
    XX(16, BIND, BIND)               \
    XX(17, REBIND, REBIND)           \
    XX(18, UNBIND, UNBIND)           \
    XX(19, ACL, ACL)                 \
    XX(20, REPORT, REPORT)           \
    XX(21, MKACTIVITY, MKACTIVITY)   \
    XX(22, CHECKOUT, CHECKOUT)       \
    XX(23, MERGE, MERGE)             \
    XX(24, MSEARCH, M - SEARCH)      \
    XX(25, NOTIFY, NOTIFY)           \
    XX(26, SUBSCRIBE, SUBSCRIBE)     \
    XX(27, UNSUBSCRIBE, UNSUBSCRIBE) \
    XX(28, PATCH, PATCH)             \
    XX(29, PURGE, PURGE)             \
    XX(30, MKCALENDAR, MKCALENDAR)   \
    XX(31, LINK, LINK)               \
    XX(32, UNLINK, UNLINK)           \
    XX(33, SOURCE, SOURCE)           \
    XX(46, QUERY, QUERY)

#define RTSP_METHOD_MAP(XX)              \
    XX(1, GET, GET)                      \
    XX(3, POST, POST)                    \
    XX(6, OPTIONS, OPTIONS)              \
    XX(35, DESCRIBE, DESCRIBE)           \
    XX(36, ANNOUNCE, ANNOUNCE)           \
    XX(37, SETUP, SETUP)                 \
    XX(38, PLAY, PLAY)                   \
    XX(39, PAUSE, PAUSE)                 \
    XX(40, TEARDOWN, TEARDOWN)           \
    XX(41, GET_PARAMETER, GET_PARAMETER) \
    XX(42, SET_PARAMETER, SET_PARAMETER) \
    XX(43, REDIRECT, REDIRECT)           \
    XX(44, RECORD, RECORD)               \
    XX(45, FLUSH, FLUSH)

#define HTTP_ALL_METHOD_MAP(XX)          \
    XX(0, DELETE, DELETE)                \
    XX(1, GET, GET)                      \
    XX(2, HEAD, HEAD)                    \
    XX(3, POST, POST)                    \
    XX(4, PUT, PUT)                      \
    XX(5, CONNECT, CONNECT)              \
    XX(6, OPTIONS, OPTIONS)              \
    XX(7, TRACE, TRACE)                  \
    XX(8, COPY, COPY)                    \
    XX(9, LOCK, LOCK)                    \
    XX(10, MKCOL, MKCOL)                 \
    XX(11, MOVE, MOVE)                   \
    XX(12, PROPFIND, PROPFIND)           \
    XX(13, PROPPATCH, PROPPATCH)         \
    XX(14, SEARCH, SEARCH)               \
    XX(15, UNLOCK, UNLOCK)               \
    XX(16, BIND, BIND)                   \
    XX(17, REBIND, REBIND)               \
    XX(18, UNBIND, UNBIND)               \
    XX(19, ACL, ACL)                     \
    XX(20, REPORT, REPORT)               \
    XX(21, MKACTIVITY, MKACTIVITY)       \
    XX(22, CHECKOUT, CHECKOUT)           \
    XX(23, MERGE, MERGE)                 \
    XX(24, MSEARCH, M - SEARCH)          \
    XX(25, NOTIFY, NOTIFY)               \
    XX(26, SUBSCRIBE, SUBSCRIBE)         \
    XX(27, UNSUBSCRIBE, UNSUBSCRIBE)     \
    XX(28, PATCH, PATCH)                 \
    XX(29, PURGE, PURGE)                 \
    XX(30, MKCALENDAR, MKCALENDAR)       \
    XX(31, LINK, LINK)                   \
    XX(32, UNLINK, UNLINK)               \
    XX(33, SOURCE, SOURCE)               \
    XX(34, PRI, PRI)                     \
    XX(35, DESCRIBE, DESCRIBE)           \
    XX(36, ANNOUNCE, ANNOUNCE)           \
    XX(37, SETUP, SETUP)                 \
    XX(38, PLAY, PLAY)                   \
    XX(39, PAUSE, PAUSE)                 \
    XX(40, TEARDOWN, TEARDOWN)           \
    XX(41, GET_PARAMETER, GET_PARAMETER) \
    XX(42, SET_PARAMETER, SET_PARAMETER) \
    XX(43, REDIRECT, REDIRECT)           \
    XX(44, RECORD, RECORD)               \
    XX(45, FLUSH, FLUSH)                 \
    XX(46, QUERY, QUERY)

#define HTTP_STATUS_MAP(XX)                                                                         \
    XX(100, CONTINUE, CONTINUE)                                                                     \
    XX(101, SWITCHING_PROTOCOLS, SWITCHING_PROTOCOLS)                                               \
    XX(102, PROCESSING, PROCESSING)                                                                 \
    XX(103, EARLY_HINTS, EARLY_HINTS)                                                               \
    XX(110, RESPONSE_IS_STALE, RESPONSE_IS_STALE)                                                   \
    XX(111, REVALIDATION_FAILED, REVALIDATION_FAILED)                                               \
    XX(112, DISCONNECTED_OPERATION, DISCONNECTED_OPERATION)                                         \
    XX(113, HEURISTIC_EXPIRATION, HEURISTIC_EXPIRATION)                                             \
    XX(199, MISCELLANEOUS_WARNING, MISCELLANEOUS_WARNING)                                           \
    XX(200, OK, OK)                                                                                 \
    XX(201, CREATED, CREATED)                                                                       \
    XX(202, ACCEPTED, ACCEPTED)                                                                     \
    XX(203, NON_AUTHORITATIVE_INFORMATION, NON_AUTHORITATIVE_INFORMATION)                           \
    XX(204, NO_CONTENT, NO_CONTENT)                                                                 \
    XX(205, RESET_CONTENT, RESET_CONTENT)                                                           \
    XX(206, PARTIAL_CONTENT, PARTIAL_CONTENT)                                                       \
    XX(207, MULTI_STATUS, MULTI_STATUS)                                                             \
    XX(208, ALREADY_REPORTED, ALREADY_REPORTED)                                                     \
    XX(214, TRANSFORMATION_APPLIED, TRANSFORMATION_APPLIED)                                         \
    XX(226, IM_USED, IM_USED)                                                                       \
    XX(299, MISCELLANEOUS_PERSISTENT_WARNING, MISCELLANEOUS_PERSISTENT_WARNING)                     \
    XX(300, MULTIPLE_CHOICES, MULTIPLE_CHOICES)                                                     \
    XX(301, MOVED_PERMANENTLY, MOVED_PERMANENTLY)                                                   \
    XX(302, FOUND, FOUND)                                                                           \
    XX(303, SEE_OTHER, SEE_OTHER)                                                                   \
    XX(304, NOT_MODIFIED, NOT_MODIFIED)                                                             \
    XX(305, USE_PROXY, USE_PROXY)                                                                   \
    XX(306, SWITCH_PROXY, SWITCH_PROXY)                                                             \
    XX(307, TEMPORARY_REDIRECT, TEMPORARY_REDIRECT)                                                 \
    XX(308, PERMANENT_REDIRECT, PERMANENT_REDIRECT)                                                 \
    XX(400, BAD_REQUEST, BAD_REQUEST)                                                               \
    XX(401, UNAUTHORIZED, UNAUTHORIZED)                                                             \
    XX(402, PAYMENT_REQUIRED, PAYMENT_REQUIRED)                                                     \
    XX(403, FORBIDDEN, FORBIDDEN)                                                                   \
    XX(404, NOT_FOUND, NOT_FOUND)                                                                   \
    XX(405, METHOD_NOT_ALLOWED, METHOD_NOT_ALLOWED)                                                 \
    XX(406, NOT_ACCEPTABLE, NOT_ACCEPTABLE)                                                         \
    XX(407, PROXY_AUTHENTICATION_REQUIRED, PROXY_AUTHENTICATION_REQUIRED)                           \
    XX(408, REQUEST_TIMEOUT, REQUEST_TIMEOUT)                                                       \
    XX(409, CONFLICT, CONFLICT)                                                                     \
    XX(410, GONE, GONE)                                                                             \
    XX(411, LENGTH_REQUIRED, LENGTH_REQUIRED)                                                       \
    XX(412, PRECONDITION_FAILED, PRECONDITION_FAILED)                                               \
    XX(413, PAYLOAD_TOO_LARGE, PAYLOAD_TOO_LARGE)                                                   \
    XX(414, URI_TOO_LONG, URI_TOO_LONG)                                                             \
    XX(415, UNSUPPORTED_MEDIA_TYPE, UNSUPPORTED_MEDIA_TYPE)                                         \
    XX(416, RANGE_NOT_SATISFIABLE, RANGE_NOT_SATISFIABLE)                                           \
    XX(417, EXPECTATION_FAILED, EXPECTATION_FAILED)                                                 \
    XX(418, IM_A_TEAPOT, IM_A_TEAPOT)                                                               \
    XX(419, PAGE_EXPIRED, PAGE_EXPIRED)                                                             \
    XX(420, ENHANCE_YOUR_CALM, ENHANCE_YOUR_CALM)                                                   \
    XX(421, MISDIRECTED_REQUEST, MISDIRECTED_REQUEST)                                               \
    XX(422, UNPROCESSABLE_ENTITY, UNPROCESSABLE_ENTITY)                                             \
    XX(423, LOCKED, LOCKED)                                                                         \
    XX(424, FAILED_DEPENDENCY, FAILED_DEPENDENCY)                                                   \
    XX(425, TOO_EARLY, TOO_EARLY)                                                                   \
    XX(426, UPGRADE_REQUIRED, UPGRADE_REQUIRED)                                                     \
    XX(428, PRECONDITION_REQUIRED, PRECONDITION_REQUIRED)                                           \
    XX(429, TOO_MANY_REQUESTS, TOO_MANY_REQUESTS)                                                   \
    XX(430, REQUEST_HEADER_FIELDS_TOO_LARGE_UNOFFICIAL, REQUEST_HEADER_FIELDS_TOO_LARGE_UNOFFICIAL) \
    XX(431, REQUEST_HEADER_FIELDS_TOO_LARGE, REQUEST_HEADER_FIELDS_TOO_LARGE)                       \
    XX(440, LOGIN_TIMEOUT, LOGIN_TIMEOUT)                                                           \
    XX(444, NO_RESPONSE, NO_RESPONSE)                                                               \
    XX(449, RETRY_WITH, RETRY_WITH)                                                                 \
    XX(450, BLOCKED_BY_PARENTAL_CONTROL, BLOCKED_BY_PARENTAL_CONTROL)                               \
    XX(451, UNAVAILABLE_FOR_LEGAL_REASONS, UNAVAILABLE_FOR_LEGAL_REASONS)                           \
    XX(460, CLIENT_CLOSED_LOAD_BALANCED_REQUEST, CLIENT_CLOSED_LOAD_BALANCED_REQUEST)               \
    XX(463, INVALID_X_FORWARDED_FOR, INVALID_X_FORWARDED_FOR)                                       \
    XX(494, REQUEST_HEADER_TOO_LARGE, REQUEST_HEADER_TOO_LARGE)                                     \
    XX(495, SSL_CERTIFICATE_ERROR, SSL_CERTIFICATE_ERROR)                                           \
    XX(496, SSL_CERTIFICATE_REQUIRED, SSL_CERTIFICATE_REQUIRED)                                     \
    XX(497, HTTP_REQUEST_SENT_TO_HTTPS_PORT, HTTP_REQUEST_SENT_TO_HTTPS_PORT)                       \
    XX(498, INVALID_TOKEN, INVALID_TOKEN)                                                           \
    XX(499, CLIENT_CLOSED_REQUEST, CLIENT_CLOSED_REQUEST)                                           \
    XX(500, INTERNAL_SERVER_ERROR, INTERNAL_SERVER_ERROR)                                           \
    XX(501, NOT_IMPLEMENTED, NOT_IMPLEMENTED)                                                       \
    XX(502, BAD_GATEWAY, BAD_GATEWAY)                                                               \
    XX(503, SERVICE_UNAVAILABLE, SERVICE_UNAVAILABLE)                                               \
    XX(504, GATEWAY_TIMEOUT, GATEWAY_TIMEOUT)                                                       \
    XX(505, HTTP_VERSION_NOT_SUPPORTED, HTTP_VERSION_NOT_SUPPORTED)                                 \
    XX(506, VARIANT_ALSO_NEGOTIATES, VARIANT_ALSO_NEGOTIATES)                                       \
    XX(507, INSUFFICIENT_STORAGE, INSUFFICIENT_STORAGE)                                             \
    XX(508, LOOP_DETECTED, LOOP_DETECTED)                                                           \
    XX(509, BANDWIDTH_LIMIT_EXCEEDED, BANDWIDTH_LIMIT_EXCEEDED)                                     \
    XX(510, NOT_EXTENDED, NOT_EXTENDED)                                                             \
    XX(511, NETWORK_AUTHENTICATION_REQUIRED, NETWORK_AUTHENTICATION_REQUIRED)                       \
    XX(520, WEB_SERVER_UNKNOWN_ERROR, WEB_SERVER_UNKNOWN_ERROR)                                     \
    XX(521, WEB_SERVER_IS_DOWN, WEB_SERVER_IS_DOWN)                                                 \
    XX(522, CONNECTION_TIMEOUT, CONNECTION_TIMEOUT)                                                 \
    XX(523, ORIGIN_IS_UNREACHABLE, ORIGIN_IS_UNREACHABLE)                                           \
    XX(524, TIMEOUT_OCCURED, TIMEOUT_OCCURED)                                                       \
    XX(525, SSL_HANDSHAKE_FAILED, SSL_HANDSHAKE_FAILED)                                             \
    XX(526, INVALID_SSL_CERTIFICATE, INVALID_SSL_CERTIFICATE)                                       \
    XX(527, RAILGUN_ERROR, RAILGUN_ERROR)                                                           \
    XX(529, SITE_IS_OVERLOADED, SITE_IS_OVERLOADED)                                                 \
    XX(530, SITE_IS_FROZEN, SITE_IS_FROZEN)                                                         \
    XX(561, IDENTITY_PROVIDER_AUTHENTICATION_ERROR, IDENTITY_PROVIDER_AUTHENTICATION_ERROR)         \
    XX(598, NETWORK_READ_TIMEOUT, NETWORK_READ_TIMEOUT)                                             \
    XX(599, NETWORK_CONNECT_TIMEOUT, NETWORK_CONNECT_TIMEOUT)

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* LLLLHTTP_C_HEADERS_ */

#ifndef INCLUDE_LLHTTP_API_H_
#define INCLUDE_LLHTTP_API_H_
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>

#if defined(__wasm__)
#define LLHTTP_EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#define LLHTTP_EXPORT __declspec(dllexport)
#else
#define LLHTTP_EXPORT
#endif

typedef llhttp__internal_t llhttp_t;
typedef struct llhttp_settings_s llhttp_settings_t;

typedef int (*llhttp_data_cb)(llhttp_t *, const char *at, size_t length);
typedef int (*llhttp_cb)(llhttp_t *);

struct llhttp_settings_s {
    /* Possible return values 0, -1, `HPE_PAUSED` */
    llhttp_cb on_message_begin;

    /* Possible return values 0, -1, HPE_USER */
    llhttp_data_cb on_protocol;
    llhttp_data_cb on_url;
    llhttp_data_cb on_status;
    llhttp_data_cb on_method;
    llhttp_data_cb on_version;
    llhttp_data_cb on_header_field;
    llhttp_data_cb on_header_value;
    llhttp_data_cb on_chunk_extension_name;
    llhttp_data_cb on_chunk_extension_value;

    /* Possible return values:
     * 0  - Proceed normally
     * 1  - Assume that request/response has no body, and proceed to parsing the
     *      next message
     * 2  - Assume absence of body (as above) and make `llhttp_execute()` return
     *      `HPE_PAUSED_UPGRADE`
     * -1 - Error
     * `HPE_PAUSED`
     */
    llhttp_cb on_headers_complete;

    /* Possible return values 0, -1, HPE_USER */
    llhttp_data_cb on_body;

    /* Possible return values 0, -1, `HPE_PAUSED` */
    llhttp_cb on_message_complete;
    llhttp_cb on_protocol_complete;
    llhttp_cb on_url_complete;
    llhttp_cb on_status_complete;
    llhttp_cb on_method_complete;
    llhttp_cb on_version_complete;
    llhttp_cb on_header_field_complete;
    llhttp_cb on_header_value_complete;
    llhttp_cb on_chunk_extension_name_complete;
    llhttp_cb on_chunk_extension_value_complete;

    /* When on_chunk_header is called, the current chunk length is stored
     * in parser->content_length.
     * Possible return values 0, -1, `HPE_PAUSED`
     */
    llhttp_cb on_chunk_header;
    llhttp_cb on_chunk_complete;
    llhttp_cb on_reset;
};

/* Initialize the parser with specific type and user settings.
 *
 * NOTE: lifetime of `settings` has to be at least the same as the lifetime of
 * the `parser` here. In practice, `settings` has to be either a static
 * variable or be allocated with `malloc`, `new`, etc.
 */
LLHTTP_EXPORT
void llhttp_init(llhttp_t *parser, llhttp_type_t type,
                 const llhttp_settings_t *settings);

LLHTTP_EXPORT
llhttp_t *llhttp_alloc(llhttp_type_t type);

LLHTTP_EXPORT
void llhttp_free(llhttp_t *parser);

LLHTTP_EXPORT
uint8_t llhttp_get_type(llhttp_t *parser);

LLHTTP_EXPORT
uint8_t llhttp_get_http_major(llhttp_t *parser);

LLHTTP_EXPORT
uint8_t llhttp_get_http_minor(llhttp_t *parser);

LLHTTP_EXPORT
uint8_t llhttp_get_method(llhttp_t *parser);

LLHTTP_EXPORT
int llhttp_get_status_code(llhttp_t *parser);

LLHTTP_EXPORT
uint8_t llhttp_get_upgrade(llhttp_t *parser);

/* Reset an already initialized parser back to the start state, preserving the
 * existing parser type, callback settings, user data, and lenient flags.
 */
LLHTTP_EXPORT
void llhttp_reset(llhttp_t *parser);

/* Initialize the settings object */
LLHTTP_EXPORT
void llhttp_settings_init(llhttp_settings_t *settings);

/* Parse full or partial request/response, invoking user callbacks along the
 * way.
 *
 * If any of `llhttp_data_cb` returns errno not equal to `HPE_OK` - the parsing
 * interrupts, and such errno is returned from `llhttp_execute()`. If
 * `HPE_PAUSED` was used as a errno, the execution can be resumed with
 * `llhttp_resume()` call.
 *
 * In a special case of CONNECT/Upgrade request/response `HPE_PAUSED_UPGRADE`
 * is returned after fully parsing the request/response. If the user wishes to
 * continue parsing, they need to invoke `llhttp_resume_after_upgrade()`.
 *
 * NOTE: if this function ever returns a non-pause type error, it will continue
 * to return the same error upon each successive call up until `llhttp_init()`
 * is called.
 */
LLHTTP_EXPORT
llhttp_errno_t llhttp_execute(llhttp_t *parser, const char *data, size_t len);

/* This method should be called when the other side has no further bytes to
 * send (e.g. shutdown of readable side of the TCP connection.)
 *
 * Requests without `Content-Length` and other messages might require treating
 * all incoming bytes as the part of the body, up to the last byte of the
 * connection. This method will invoke `on_message_complete()` callback if the
 * request was terminated safely. Otherwise a error code would be returned.
 */
LLHTTP_EXPORT
llhttp_errno_t llhttp_finish(llhttp_t *parser);

/* Returns `1` if the incoming message is parsed until the last byte, and has
 * to be completed by calling `llhttp_finish()` on EOF
 */
LLHTTP_EXPORT
int llhttp_message_needs_eof(const llhttp_t *parser);

/* Returns `1` if there might be any other messages following the last that was
 * successfully parsed.
 */
LLHTTP_EXPORT
int llhttp_should_keep_alive(const llhttp_t *parser);

/* Make further calls of `llhttp_execute()` return `HPE_PAUSED` and set
 * appropriate error reason.
 *
 * Important: do not call this from user callbacks! User callbacks must return
 * `HPE_PAUSED` if pausing is required.
 */
LLHTTP_EXPORT
void llhttp_pause(llhttp_t *parser);

/* Might be called to resume the execution after the pause in user's callback.
 * See `llhttp_execute()` above for details.
 *
 * Call this only if `llhttp_execute()` returns `HPE_PAUSED`.
 */
LLHTTP_EXPORT
void llhttp_resume(llhttp_t *parser);

/* Might be called to resume the execution after the pause in user's callback.
 * See `llhttp_execute()` above for details.
 *
 * Call this only if `llhttp_execute()` returns `HPE_PAUSED_UPGRADE`
 */
LLHTTP_EXPORT
void llhttp_resume_after_upgrade(llhttp_t *parser);

/* Returns the latest return error */
LLHTTP_EXPORT
llhttp_errno_t llhttp_get_errno(const llhttp_t *parser);

/* Returns the verbal explanation of the latest returned error.
 *
 * Note: User callback should set error reason when returning the error. See
 * `llhttp_set_error_reason()` for details.
 */
LLHTTP_EXPORT
const char *llhttp_get_error_reason(const llhttp_t *parser);

/* Assign verbal description to the returned error. Must be called in user
 * callbacks right before returning the errno.
 *
 * Note: `HPE_USER` error code might be useful in user callbacks.
 */
LLHTTP_EXPORT
void llhttp_set_error_reason(llhttp_t *parser, const char *reason);

/* Returns the pointer to the last parsed byte before the returned error. The
 * pointer is relative to the `data` argument of `llhttp_execute()`.
 *
 * Note: this method might be useful for counting the number of parsed bytes.
 */
LLHTTP_EXPORT
const char *llhttp_get_error_pos(const llhttp_t *parser);

/* Returns textual name of error code */
LLHTTP_EXPORT
const char *llhttp_errno_name(llhttp_errno_t err);

/* Returns textual name of HTTP method */
LLHTTP_EXPORT
const char *llhttp_method_name(llhttp_method_t method);

/* Returns textual name of HTTP status */
LLHTTP_EXPORT
const char *llhttp_status_name(llhttp_status_t status);

/* Enables/disables lenient header value parsing (disabled by default).
 *
 * Lenient parsing disables header value token checks, extending llhttp's
 * protocol support to highly non-compliant clients/server. No
 * `HPE_INVALID_HEADER_TOKEN` will be raised for incorrect header values when
 * lenient parsing is "on".
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_headers(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of conflicting `Transfer-Encoding` and
 * `Content-Length` headers (disabled by default).
 *
 * Normally `llhttp` would error when `Transfer-Encoding` is present in
 * conjunction with `Content-Length`. This error is important to prevent HTTP
 * request smuggling, but may be less desirable for small number of cases
 * involving legacy servers.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_chunked_length(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of `Connection: close` and HTTP/1.0
 * requests responses.
 *
 * Normally `llhttp` would error on (in strict mode) or discard (in loose mode)
 * the HTTP request/response after the request/response with `Connection: close`
 * and `Content-Length`. This is important to prevent cache poisoning attacks,
 * but might interact badly with outdated and insecure clients. With this flag
 * the extra request/response will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * poisoning attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_keep_alive(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of `Transfer-Encoding` header.
 *
 * Normally `llhttp` would error when a `Transfer-Encoding` has `chunked` value
 * and another value after it (either in a single header or in multiple
 * headers whose value are internally joined using `, `).
 * This is mandated by the spec to reliably determine request body size and thus
 * avoid request smuggling.
 * With this flag the extra value will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_transfer_encoding(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of HTTP version.
 *
 * Normally `llhttp` would error when the HTTP version in the request or status line
 * is not `0.9`, `1.0`, `1.1` or `2.0`.
 * With this flag the invalid value will be parsed normally.
 *
 * **Enabling this flag can pose a security issue since you will allow unsupported
 * HTTP versions. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_version(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of additional data received after a message ends
 * and keep-alive is disabled.
 *
 * Normally `llhttp` would error when additional unexpected data is received if the message
 * contains the `Connection` header with `close` value.
 * With this flag the extra data will discarded without throwing an error.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * poisoning attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_data_after_close(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of incomplete CRLF sequences.
 *
 * Normally `llhttp` would error when a CR is not followed by LF when terminating the
 * request line, the status line, the headers or a chunk header.
 * With this flag only a CR is required to terminate such sections.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_lf_after_cr(llhttp_t *parser, int enabled);

/*
 * Enables/disables lenient handling of line separators.
 *
 * Normally `llhttp` would error when a LF is not preceded by CR when terminating the
 * request line, the status line, the headers, a chunk header or a chunk data.
 * With this flag only a LF is required to terminate such sections.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_cr_before_lf(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of chunks not separated via CRLF.
 *
 * Normally `llhttp` would error when after a chunk data a CRLF is missing before
 * starting a new chunk.
 * With this flag the new chunk can start immediately after the previous one.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_optional_crlf_after_chunk(llhttp_t *parser, int enabled);

/* Enables/disables lenient handling of spaces after chunk size.
 *
 * Normally `llhttp` would error when after a chunk size is followed by one or more
 * spaces are present instead of a CRLF or `;`.
 * With this flag this check is disabled.
 *
 * **Enabling this flag can pose a security issue since you will be exposed to
 * request smuggling attacks. USE WITH CAUTION!**
 */
LLHTTP_EXPORT
void llhttp_set_lenient_spaces_after_chunk_size(llhttp_t *parser, int enabled);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* INCLUDE_LLHTTP_API_H_ */

#endif /* INCLUDE_LLHTTP_H_ */
