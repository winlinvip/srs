//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_protocol_http_stack.hpp>

#include <algorithm>
#include <sstream>
#include <stdlib.h>
using namespace std;

#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_file.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_json.hpp>
#include <srs_protocol_utility.hpp>

#define SRS_HTTP_DEFAULT_PAGE "index.html"

// @see ISrsHttpMessage._http_ts_send_buffer
#define SRS_HTTP_TS_SEND_BUFFER_SIZE 4096

#define SRS_HTTP_AUTH_SCHEME_BASIC "Basic"
#define SRS_HTTP_AUTH_PREFIX_BASIC SRS_HTTP_AUTH_SCHEME_BASIC " "

// Calculate the output size needed to base64-encode x bytes to a null-terminated string.
#define SRS_AV_BASE64_SIZE(x) (((x) + 2) / 3 * 4 + 1)

// We use the standard encoding:
//      var StdEncoding = NewEncoding(encodeStd)
// StdEncoding is the standard base64 encoding, as defined in RFC 4648.
namespace
{
char padding = '=';
string encoder = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace
// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_decode(string cipher, string &plaintext)
{
    srs_error_t err = srs_success;

    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }

    // decode is like Decode but returns an additional 'end' value, which
    // indicates if end-of-message padding or a partial quantum was encountered
    // and thus any additional data is an error.
    int si = 0;

    // skip over newlines
    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
    }

    for (bool end = false; si < (int)cipher.length() && !end;) {
        // Decode quantum using the base64 alphabet
        uint8_t dbuf[4];
        memset(dbuf, 0x00, sizeof(dbuf));

        int dinc = 3;
        int dlen = 4;
        srs_assert(dinc > 0);

        for (int j = 0; j < (int)sizeof(dbuf); j++) {
            if (si == (int)cipher.length()) {
                if (padding != -1 || j < 2) {
                    return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }

                dinc = j - 1;
                dlen = j;
                end = true;
                break;
            }

            char in = cipher.at(si);

            si++;
            // skip over newlines
            for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
            }

            if (in == padding) {
                // We've reached the end and there's padding
                switch (j) {
                case 0:
                case 1:
                    // incorrect padding
                    return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                case 2:
                    // "==" is expected, the first "=" is already consumed.
                    if (si == (int)cipher.length()) {
                        return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    }
                    if (cipher.at(si) != padding) {
                        // incorrect padding
                        return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                    }

                    si++;
                    // skip over newlines
                    for (; si < (int)cipher.length() && (cipher.at(si) == '\n' || cipher.at(si) == '\r'); si++) {
                    }
                }

                if (si < (int)cipher.length()) {
                    // trailing garbage
                    err = srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
                }
                dinc = 3;
                dlen = j;
                end = true;
                break;
            }

            dbuf[j] = decodeMap[(uint8_t)in];
            if (dbuf[j] == 0xff) {
                return srs_error_new(ERROR_BASE64_DECODE, "corrupt input at %d", si);
            }
        }

        // Convert 4x 6bit source bytes into 3 bytes
        uint32_t val = uint32_t(dbuf[0]) << 18 | uint32_t(dbuf[1]) << 12 | uint32_t(dbuf[2]) << 6 | uint32_t(dbuf[3]);
        if (dlen >= 2) {
            plaintext.append(1, char(val >> 16));
        }
        if (dlen >= 3) {
            plaintext.append(1, char(val >> 8));
        }
        if (dlen >= 4) {
            plaintext.append(1, char(val));
        }
    }

    return err;
}

// @see golang encoding/base64/base64.go
srs_error_t srs_av_base64_encode(std::string plaintext, std::string &cipher)
{
    srs_error_t err = srs_success;
    uint8_t decodeMap[256];
    memset(decodeMap, 0xff, sizeof(decodeMap));

    for (int i = 0; i < (int)encoder.length(); i++) {
        decodeMap[(uint8_t)encoder.at(i)] = uint8_t(i);
    }
    cipher.clear();

    uint32_t val = 0;
    int si = 0;
    int n = (plaintext.length() / 3) * 3;
    uint8_t *p = (uint8_t *)plaintext.c_str();
    while (si < n) {
        // Convert 3x 8bit source bytes into 4 bytes
        val = (uint32_t(p[si + 0]) << 16) | (uint32_t(p[si + 1]) << 8) | uint32_t(p[si + 2]);

        cipher += encoder[val >> 18 & 0x3f];
        cipher += encoder[val >> 12 & 0x3f];
        cipher += encoder[val >> 6 & 0x3f];
        cipher += encoder[val & 0x3f];

        si += 3;
    }

    int remain = plaintext.length() - si;
    if (0 == remain) {
        return err;
    }

    val = uint32_t(p[si + 0]) << 16;
    if (2 == remain) {
        val |= uint32_t(p[si + 1]) << 8;
    }

    cipher += encoder[val >> 18 & 0x3f];
    cipher += encoder[val >> 12 & 0x3f];

    switch (remain) {
    case 2:
        cipher += encoder[val >> 6 & 0x3f];
        cipher += padding;
        break;
    case 1:
        cipher += padding;
        cipher += padding;
        break;
    }

    return err;
}

// get the status text of code.
string srs_generate_http_status_text(int status)
{
    static std::map<int, std::string> _status_map;
    if (_status_map.empty()) {
        _status_map[SRS_CONSTS_HTTP_Continue] = SRS_CONSTS_HTTP_Continue_str;
        _status_map[SRS_CONSTS_HTTP_SwitchingProtocols] = SRS_CONSTS_HTTP_SwitchingProtocols_str;
        _status_map[SRS_CONSTS_HTTP_OK] = SRS_CONSTS_HTTP_OK_str;
        _status_map[SRS_CONSTS_HTTP_Created] = SRS_CONSTS_HTTP_Created_str;
        _status_map[SRS_CONSTS_HTTP_Accepted] = SRS_CONSTS_HTTP_Accepted_str;
        _status_map[SRS_CONSTS_HTTP_NonAuthoritativeInformation] = SRS_CONSTS_HTTP_NonAuthoritativeInformation_str;
        _status_map[SRS_CONSTS_HTTP_NoContent] = SRS_CONSTS_HTTP_NoContent_str;
        _status_map[SRS_CONSTS_HTTP_ResetContent] = SRS_CONSTS_HTTP_ResetContent_str;
        _status_map[SRS_CONSTS_HTTP_PartialContent] = SRS_CONSTS_HTTP_PartialContent_str;
        _status_map[SRS_CONSTS_HTTP_MultipleChoices] = SRS_CONSTS_HTTP_MultipleChoices_str;
        _status_map[SRS_CONSTS_HTTP_MovedPermanently] = SRS_CONSTS_HTTP_MovedPermanently_str;
        _status_map[SRS_CONSTS_HTTP_Found] = SRS_CONSTS_HTTP_Found_str;
        _status_map[SRS_CONSTS_HTTP_SeeOther] = SRS_CONSTS_HTTP_SeeOther_str;
        _status_map[SRS_CONSTS_HTTP_NotModified] = SRS_CONSTS_HTTP_NotModified_str;
        _status_map[SRS_CONSTS_HTTP_UseProxy] = SRS_CONSTS_HTTP_UseProxy_str;
        _status_map[SRS_CONSTS_HTTP_TemporaryRedirect] = SRS_CONSTS_HTTP_TemporaryRedirect_str;
        _status_map[SRS_CONSTS_HTTP_BadRequest] = SRS_CONSTS_HTTP_BadRequest_str;
        _status_map[SRS_CONSTS_HTTP_Unauthorized] = SRS_CONSTS_HTTP_Unauthorized_str;
        _status_map[SRS_CONSTS_HTTP_PaymentRequired] = SRS_CONSTS_HTTP_PaymentRequired_str;
        _status_map[SRS_CONSTS_HTTP_Forbidden] = SRS_CONSTS_HTTP_Forbidden_str;
        _status_map[SRS_CONSTS_HTTP_NotFound] = SRS_CONSTS_HTTP_NotFound_str;
        _status_map[SRS_CONSTS_HTTP_MethodNotAllowed] = SRS_CONSTS_HTTP_MethodNotAllowed_str;
        _status_map[SRS_CONSTS_HTTP_NotAcceptable] = SRS_CONSTS_HTTP_NotAcceptable_str;
        _status_map[SRS_CONSTS_HTTP_ProxyAuthenticationRequired] = SRS_CONSTS_HTTP_ProxyAuthenticationRequired_str;
        _status_map[SRS_CONSTS_HTTP_RequestTimeout] = SRS_CONSTS_HTTP_RequestTimeout_str;
        _status_map[SRS_CONSTS_HTTP_Conflict] = SRS_CONSTS_HTTP_Conflict_str;
        _status_map[SRS_CONSTS_HTTP_Gone] = SRS_CONSTS_HTTP_Gone_str;
        _status_map[SRS_CONSTS_HTTP_LengthRequired] = SRS_CONSTS_HTTP_LengthRequired_str;
        _status_map[SRS_CONSTS_HTTP_PreconditionFailed] = SRS_CONSTS_HTTP_PreconditionFailed_str;
        _status_map[SRS_CONSTS_HTTP_RequestEntityTooLarge] = SRS_CONSTS_HTTP_RequestEntityTooLarge_str;
        _status_map[SRS_CONSTS_HTTP_RequestURITooLarge] = SRS_CONSTS_HTTP_RequestURITooLarge_str;
        _status_map[SRS_CONSTS_HTTP_UnsupportedMediaType] = SRS_CONSTS_HTTP_UnsupportedMediaType_str;
        _status_map[SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable] = SRS_CONSTS_HTTP_RequestedRangeNotSatisfiable_str;
        _status_map[SRS_CONSTS_HTTP_ExpectationFailed] = SRS_CONSTS_HTTP_ExpectationFailed_str;
        _status_map[SRS_CONSTS_HTTP_InternalServerError] = SRS_CONSTS_HTTP_InternalServerError_str;
        _status_map[SRS_CONSTS_HTTP_NotImplemented] = SRS_CONSTS_HTTP_NotImplemented_str;
        _status_map[SRS_CONSTS_HTTP_BadGateway] = SRS_CONSTS_HTTP_BadGateway_str;
        _status_map[SRS_CONSTS_HTTP_ServiceUnavailable] = SRS_CONSTS_HTTP_ServiceUnavailable_str;
        _status_map[SRS_CONSTS_HTTP_GatewayTimeout] = SRS_CONSTS_HTTP_GatewayTimeout_str;
        _status_map[SRS_CONSTS_HTTP_HTTPVersionNotSupported] = SRS_CONSTS_HTTP_HTTPVersionNotSupported_str;
    }

    std::string status_text;
    if (_status_map.find(status) == _status_map.end()) {
        status_text = "Status Unknown";
    } else {
        status_text = _status_map[status];
    }

    return status_text;
}

// bodyAllowedForStatus reports whether a given response status code
// permits a body.  See RFC2616, section 4.4.
bool srs_go_http_body_allowd(int status)
{
    if (status >= SRS_CONSTS_HTTP_Continue && status < SRS_CONSTS_HTTP_OK) {
        return false;
    } else if (status == SRS_CONSTS_HTTP_NoContent || status == SRS_CONSTS_HTTP_NotModified) {
        return false;
    }

    return true;
}

// DetectContentType implements the algorithm described
// at http://mimesniff.spec.whatwg.org/ to determine the
// Content-Type of the given data.  It considers at most the
// first 512 bytes of data.  DetectContentType always returns
// a valid MIME type: if it cannot determine a more specific one, it
// returns "application/octet-stream".
string srs_go_http_detect(char *data, int size)
{
    // TODO: Implement the request content-type detecting.
    return "application/octet-stream"; // fallback
}

srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code)
{
    return srs_go_http_error(w, code, srs_generate_http_status_text(code));
}

srs_error_t srs_go_http_error(ISrsHttpResponseWriter *w, int code, string error)
{
    srs_error_t err = srs_success;

    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(error.length());
    w->write_header(code);

    if ((err = w->write((char *)error.data(), (int)error.length())) != srs_success) {
        return srs_error_wrap(err, "http write");
    }

    return err;
}

SrsHttpHeader::SrsHttpHeader()
{
}

SrsHttpHeader::~SrsHttpHeader()
{
}

void SrsHttpHeader::set(string key, string value)
{
    // Convert to UpperCamelCase, for example:
    //      transfer-encoding
    // transform to:
    //      Transfer-Encoding
    char pchar = 0;
    for (int i = 0; i < (int)key.length(); i++) {
        char ch = key.at(i);

        if (i == 0 || pchar == '-') {
            if (ch >= 'a' && ch <= 'z') {
                ((char *)key.data())[i] = ch - 32;
            }
        }
        pchar = ch;
    }

    if (headers.find(key) == headers.end()) {
        keys_.push_back(key);
    }

    headers[key] = value;
}

string SrsHttpHeader::get(string key)
{
    std::string v;

    map<string, string>::iterator it = headers.find(key);
    if (it != headers.end()) {
        v = it->second;
    }

    return v;
}

void SrsHttpHeader::del(string key)
{
    if (true) {
        vector<string>::iterator it = std::find(keys_.begin(), keys_.end(), key);
        if (it != keys_.end()) {
            it = keys_.erase(it);
        }
    }

    if (true) {
        map<string, string>::iterator it = headers.find(key);
        if (it != headers.end()) {
            headers.erase(it);
        }
    }
}

int SrsHttpHeader::count()
{
    return (int)headers.size();
}

void SrsHttpHeader::dumps(SrsJsonObject *o)
{
    vector<string>::iterator it;
    for (it = keys_.begin(); it != keys_.end(); ++it) {
        const string &key = *it;
        const string &value = headers[key];
        o->set(key, SrsJsonAny::str(value.c_str()));
    }
}

int64_t SrsHttpHeader::content_length()
{
    std::string cl = get("Content-Length");

    if (cl.empty()) {
        return -1;
    }

    return (int64_t)::atof(cl.c_str());
}

void SrsHttpHeader::set_content_length(int64_t size)
{
    set("Content-Length", srs_strconv_format_int(size));
}

string SrsHttpHeader::content_type()
{
    return get("Content-Type");
}

void SrsHttpHeader::set_content_type(string ct)
{
    set("Content-Type", ct);
}

void SrsHttpHeader::write(stringstream &ss)
{
    vector<string>::iterator it;
    for (it = keys_.begin(); it != keys_.end(); ++it) {
        const string &key = *it;
        const string &value = headers[key];
        ss << key << ": " << value << SRS_HTTP_CRLF;
    }
}

ISrsHttpResponseWriter::ISrsHttpResponseWriter()
{
}

ISrsHttpResponseWriter::~ISrsHttpResponseWriter()
{
}

ISrsHttpResponseReader::ISrsHttpResponseReader()
{
}

ISrsHttpResponseReader::~ISrsHttpResponseReader()
{
}

ISrsHttpRequestWriter::ISrsHttpRequestWriter()
{
}

ISrsHttpRequestWriter::~ISrsHttpRequestWriter()
{
}

ISrsHttpHandler::ISrsHttpHandler()
{
    entry = NULL;
}

ISrsHttpHandler::~ISrsHttpHandler()
{
}

bool ISrsHttpHandler::is_not_found()
{
    return false;
}

SrsHttpRedirectHandler::SrsHttpRedirectHandler(string u, int c)
{
    url = u;
    code = c;
}

SrsHttpRedirectHandler::~SrsHttpRedirectHandler()
{
}

srs_error_t SrsHttpRedirectHandler::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    string location = url;
    if (!r->query().empty()) {
        location += "?" + r->query();
    }

    string msg = "Redirect to " + location;

    w->header()->set_content_type("text/plain; charset=utf-8");
    w->header()->set_content_length(msg.length());
    w->header()->set("Location", location);
    w->write_header(code);

    w->write((char *)msg.data(), (int)msg.length());
    w->final_request();

    srs_info("redirect to %s.", location.c_str());
    return srs_success;
}

SrsHttpNotFoundHandler::SrsHttpNotFoundHandler()
{
}

SrsHttpNotFoundHandler::~SrsHttpNotFoundHandler()
{
}

bool SrsHttpNotFoundHandler::is_not_found()
{
    return true;
}

srs_error_t SrsHttpNotFoundHandler::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    return srs_go_http_error(w, SRS_CONSTS_HTTP_NotFound);
}

string srs_http_fs_fullpath(string dir, string pattern, string upath)
{
    // add default pages.
    if (srs_strings_ends_with(upath, "/")) {
        upath += SRS_HTTP_DEFAULT_PAGE;
    }

    // Remove the virtual directory.
    // For example:
    //      pattern=/api, the virtual directory is api, upath=/api/index.html, fullpath={dir}/index.html
    //      pattern=/api, the virtual directory is api, upath=/api/views/index.html, fullpath={dir}/views/index.html
    // The vhost prefix is ignored, for example:
    //      pattern=ossrs.net/api, the vhost is ossrs.net, the pattern equals to /api under this vhost,
    //      so the virtual directory is also api
    size_t pos = pattern.find("/");
    string filename = upath;
    if (upath.length() > pattern.length() && pos != string::npos) {
        filename = upath.substr(pattern.length() - pos);
    }

    string fullpath = srs_strings_trim_end(dir, "/");
    if (!srs_strings_starts_with(filename, "/")) {
        fullpath += "/";
    }
    fullpath += filename;

    return fullpath;
}

SrsHttpFileServer::SrsHttpFileServer(string root_dir)
{
    dir = root_dir;
    fs_factory = new ISrsFileReaderFactory();
    _srs_path_exists = srs_path_exists;
}

SrsHttpFileServer::~SrsHttpFileServer()
{
    srs_freep(fs_factory);
}

void SrsHttpFileServer::set_fs_factory(ISrsFileReaderFactory *f)
{
    srs_freep(fs_factory);
    fs_factory = f;
}

void SrsHttpFileServer::set_path_check(_pfn_srs_path_exists pfn)
{
    _srs_path_exists = pfn;
}

srs_error_t SrsHttpFileServer::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_assert(entry);

    // For each HTTP session, we use short-term HTTP connection.
    SrsHttpHeader *hdr = w->header();
    hdr->set("Connection", "Close");

    string upath = r->path();
    string fullpath = srs_http_fs_fullpath(dir, entry->pattern, upath);
    string basename = srs_path_filepath_base(upath);

    // stat current dir, if exists, return error.
    if (!_srs_path_exists(fullpath)) {
        srs_warn("http miss file=%s, pattern=%s, upath=%s",
                 fullpath.c_str(), entry->pattern.c_str(), upath.c_str());
        return SrsHttpNotFoundHandler().serve_http(w, r);
    }
    srs_trace("http match file=%s, pattern=%s, upath=%s",
              fullpath.c_str(), entry->pattern.c_str(), upath.c_str());

    // handle file according to its extension.
    // use vod stream for .flv/.fhv
    if (srs_strings_ends_with(upath, ".flv", ".fhv")) {
        return serve_flv_file(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".m3u8")) {
        return serve_m3u8_ctx(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".ts", ".m4s") || basename == "init.mp4") {
        return serve_ts_ctx(w, r, fullpath);
    } else if (srs_strings_ends_with(upath, ".mp4")) {
        return serve_mp4_file(w, r, fullpath);
    }

    // serve common static file.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    srs_error_t err = srs_success;

    SrsUniquePtr<SrsFileReader> fs(fs_factory->create_file_reader());

    if ((err = fs->open(fullpath)) != srs_success) {
        return srs_error_wrap(err, "open file %s", fullpath.c_str());
    }

    // The length of bytes we could response to.
    int64_t length = fs->filesize() - fs->tellg();

    // unset the content length to encode in chunked encoding.
    w->header()->set_content_length(length);

    static std::map<std::string, std::string> _mime;
    if (_mime.empty()) {
        _mime[".ts"] = "video/MP2T";
        _mime[".flv"] = "video/x-flv";
        _mime[".m4v"] = "video/x-m4v";
        _mime[".3gpp"] = "video/3gpp";
        _mime[".3gp"] = "video/3gpp";
        _mime[".mp4"] = "video/mp4";
        _mime[".aac"] = "audio/x-aac";
        _mime[".mp3"] = "audio/mpeg";
        _mime[".m4a"] = "audio/x-m4a";
        _mime[".ogg"] = "audio/ogg";
        // @see hls-m3u8-draft-pantos-http-live-streaming-12.pdf, page 5.
        _mime[".m3u8"] = "application/vnd.apple.mpegurl"; // application/x-mpegURL
        _mime[".rss"] = "application/rss+xml";
        _mime[".json"] = "application/json";
        _mime[".swf"] = "application/x-shockwave-flash";
        _mime[".doc"] = "application/msword";
        _mime[".zip"] = "application/zip";
        _mime[".rar"] = "application/x-rar-compressed";
        _mime[".xml"] = "text/xml";
        _mime[".html"] = "text/html";
        _mime[".js"] = "text/javascript";
        _mime[".css"] = "text/css";
        _mime[".ico"] = "image/x-icon";
        _mime[".png"] = "image/png";
        _mime[".jpeg"] = "image/jpeg";
        _mime[".jpg"] = "image/jpeg";
        _mime[".gif"] = "image/gif";
        // For MPEG-DASH.
        _mime[".mpd"] = "application/dash+xml";
        _mime[".m4s"] = "video/iso.segment";
        _mime[".mp4v"] = "video/mp4";
    }

    if (true) {
        std::string ext = srs_path_filepath_ext(fullpath);

        if (_mime.find(ext) == _mime.end()) {
            w->header()->set_content_type("application/octet-stream");
        } else {
            w->header()->set_content_type(_mime[ext]);
        }
    }

    // Enter chunked mode, because we didn't set the content-length.
    w->write_header(SRS_CONSTS_HTTP_OK);

    // write body.
    int64_t left = length;
    if ((err = copy(w, fs.get(), r, left)) != srs_success) {
        return srs_error_wrap(err, "copy file=%s size=%" PRId64, fullpath.c_str(), left);
    }

    if ((err = w->final_request()) != srs_success) {
        return srs_error_wrap(err, "final request");
    }

    return err;
}

srs_error_t SrsHttpFileServer::serve_flv_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    std::string start = r->query_get("start");
    if (start.empty()) {
        return serve_file(w, r, fullpath);
    }

    int64_t offset = ::atoll(start.c_str());
    if (offset <= 0) {
        return serve_file(w, r, fullpath);
    }

    return serve_flv_stream(w, r, fullpath, offset);
}

srs_error_t SrsHttpFileServer::serve_mp4_file(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath)
{
    // for flash to request mp4 range in query string.
    std::string range = r->query_get("range");
    // or, use bytes to request range.
    if (range.empty()) {
        range = r->query_get("bytes");
    }

    // Fetch range from header.
    SrsHttpHeader *h = r->header();
    if (range.empty() && h) {
        range = h->get("Range");
        if (range.find("bytes=") == 0) {
            range = range.substr(6);
        }
    }

    // rollback to serve whole file.
    size_t pos = string::npos;
    if (range.empty() || (pos = range.find("-")) == string::npos) {
        return serve_file(w, r, fullpath);
    }

    // parse the start in query string
    int64_t start = 0;
    if (pos > 0) {
        start = ::atoll(range.substr(0, pos).c_str());
    }

    // parse end in query string.
    int64_t end = -1;
    if (pos < range.length() - 1) {
        end = ::atoll(range.substr(pos + 1).c_str());
    }

    // invalid param, serve as whole mp4 file.
    if (start < 0 || (end != -1 && start > end)) {
        return serve_file(w, r, fullpath);
    }

    return serve_mp4_stream(w, r, fullpath, start, end);
}

srs_error_t SrsHttpFileServer::serve_flv_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t offset)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    // TODO: FIXME: Support range in header https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Range_requests
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_mp4_stream(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, string fullpath, int64_t start, int64_t end)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    // TODO: FIXME: Support range in header https://developer.mozilla.org/zh-CN/docs/Web/HTTP/Range_requests
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_m3u8_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::serve_ts_ctx(ISrsHttpResponseWriter *w, ISrsHttpMessage *r, std::string fullpath)
{
    // @remark For common http file server, we don't support stream request, please use SrsVodStream instead.
    return serve_file(w, r, fullpath);
}

srs_error_t SrsHttpFileServer::copy(ISrsHttpResponseWriter *w, SrsFileReader *fs, ISrsHttpMessage *r, int64_t size)
{
    srs_error_t err = srs_success;

    int64_t left = size;
    SrsUniquePtr<char[]> buf(new char[SRS_HTTP_TS_SEND_BUFFER_SIZE]);

    while (left > 0) {
        ssize_t nread = -1;
        int max_read = srs_min(left, SRS_HTTP_TS_SEND_BUFFER_SIZE);
        if ((err = fs->read(buf.get(), max_read, &nread)) != srs_success) {
            return srs_error_wrap(err, "read limit=%d, left=%" PRId64, max_read, left);
        }

        left -= nread;
        if ((err = w->write(buf.get(), (int)nread)) != srs_success) {
            return srs_error_wrap(err, "write limit=%d, bytes=%d, left=%" PRId64, max_read, (int)nread, left);
        }
    }

    return err;
}

SrsHttpMuxEntry::SrsHttpMuxEntry()
{
    enabled = true;
    explicit_match = false;
    handler = NULL;
}

SrsHttpMuxEntry::~SrsHttpMuxEntry()
{
    srs_freep(handler);
}

ISrsHttpDynamicMatcher::ISrsHttpDynamicMatcher()
{
}

ISrsHttpDynamicMatcher::~ISrsHttpDynamicMatcher()
{
}

ISrsHttpServeMux::ISrsHttpServeMux() : ISrsHttpHandler()
{
}

ISrsHttpServeMux::~ISrsHttpServeMux()
{
}

SrsHttpServeMux::SrsHttpServeMux()
{
}

SrsHttpServeMux::~SrsHttpServeMux()
{
    std::map<std::string, SrsHttpMuxEntry *>::iterator it;
    for (it = static_matchers_.begin(); it != static_matchers_.end(); ++it) {
        SrsHttpMuxEntry *entry = it->second;
        srs_freep(entry);
    }
    static_matchers_.clear();

    vhosts_.clear();
    dynamic_matchers_.clear();
}

srs_error_t SrsHttpServeMux::initialize()
{
    srs_error_t err = srs_success;

    // TODO: FIXME: Implements it.

    return err;
}

void SrsHttpServeMux::add_dynamic_matcher(ISrsHttpDynamicMatcher *h)
{
    std::vector<ISrsHttpDynamicMatcher *>::iterator it;
    it = std::find(dynamic_matchers_.begin(), dynamic_matchers_.end(), h);
    if (it != dynamic_matchers_.end()) {
        return;
    }
    dynamic_matchers_.push_back(h);
}

void SrsHttpServeMux::remove_dynamic_matcher(ISrsHttpDynamicMatcher *h)
{
    std::vector<ISrsHttpDynamicMatcher *>::iterator it;
    it = std::find(dynamic_matchers_.begin(), dynamic_matchers_.end(), h);
    if (it == dynamic_matchers_.end()) {
        return;
    }
    it = dynamic_matchers_.erase(it);
}

srs_error_t SrsHttpServeMux::handle(std::string pattern, ISrsHttpHandler *handler)
{
    srs_assert(handler);

    if (pattern.empty()) {
        return srs_error_new(ERROR_HTTP_PATTERN_EMPTY, "empty pattern");
    }

    if (static_matchers_.find(pattern) != static_matchers_.end()) {
        SrsHttpMuxEntry *exists = static_matchers_[pattern];
        if (exists->explicit_match) {
            return srs_error_new(ERROR_HTTP_PATTERN_DUPLICATED, "pattern=%s exists", pattern.c_str());
        }
    }

    std::string vhost = pattern;
    if (pattern.at(0) != '/') {
        if (pattern.find("/") != string::npos) {
            vhost = pattern.substr(0, pattern.find("/"));
        }
        vhosts_[vhost] = handler;
    }

    if (true) {
        SrsHttpMuxEntry *entry = new SrsHttpMuxEntry();
        entry->explicit_match = true;
        entry->handler = handler;
        entry->pattern = pattern;
        entry->handler->entry = entry;

        if (static_matchers_.find(pattern) != static_matchers_.end()) {
            SrsHttpMuxEntry *exists = static_matchers_[pattern];
            srs_freep(exists);
        }
        static_matchers_[pattern] = entry;
    }

    // Helpful behavior:
    // If pattern is /tree/, insert an implicit permanent redirect for /tree.
    // It can be overridden by an explicit registration.
    if (pattern != "/" && !pattern.empty() && pattern.at(pattern.length() - 1) == '/') {
        std::string rpattern = pattern.substr(0, pattern.length() - 1);
        SrsHttpMuxEntry *entry = NULL;

        // free the exists implicit entry
        if (static_matchers_.find(rpattern) != static_matchers_.end()) {
            entry = static_matchers_[rpattern];
        }

        // create implicit redirect.
        if (!entry || !entry->explicit_match) {
            srs_freep(entry);

            entry = new SrsHttpMuxEntry();
            entry->explicit_match = false;
            entry->handler = new SrsHttpRedirectHandler(pattern, SRS_CONSTS_HTTP_Found);
            entry->pattern = pattern;
            entry->handler->entry = entry;

            static_matchers_[rpattern] = entry;
        }
    }

    return srs_success;
}

void SrsHttpServeMux::unhandle(std::string pattern, ISrsHttpHandler *handler)
{
    if (true) {
        std::map<std::string, SrsHttpMuxEntry *>::iterator it = static_matchers_.find(pattern);
        if (it != static_matchers_.end()) {
            SrsHttpMuxEntry *entry = it->second;
            static_matchers_.erase(it);

            // We don't free the handler, because user should free it.
            if (entry->handler == handler) {
                entry->handler = NULL;
            }

            // Should always free the entry.
            srs_freep(entry);
        }
    }

    std::string vhost = pattern;
    if (pattern.at(0) != '/') {
        if (pattern.find("/") != string::npos) {
            vhost = pattern.substr(0, pattern.find("/"));
        }

        std::map<std::string, ISrsHttpHandler *>::iterator it = vhosts_.find(vhost);
        if (it != vhosts_.end())
            vhosts_.erase(it);
    }
}

srs_error_t SrsHttpServeMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    ISrsHttpHandler *h = NULL;
    if ((err = find_handler(r, &h)) != srs_success) {
        return srs_error_wrap(err, "find handler");
    }

    srs_assert(h);
    if ((err = h->serve_http(w, r)) != srs_success) {
        return srs_error_wrap(err, "serve http");
    }

    return err;
}

srs_error_t SrsHttpServeMux::find_handler(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    srs_error_t err = srs_success;

    // TODO: FIXME: support the path . and ..
    if (r->url().find("..") != std::string::npos) {
        return srs_error_new(ERROR_HTTP_URL_NOT_CLEAN, "url %s not canonical", r->url().c_str());
    }

    if ((err = match(r, ph)) != srs_success) {
        return srs_error_wrap(err, "http match");
    }

    // always try to handle by dynamic matchers.
    if (!dynamic_matchers_.empty()) {
        // notify all dynamic matchers unless matching failed.
        std::vector<ISrsHttpDynamicMatcher *>::iterator it;
        for (it = dynamic_matchers_.begin(); it != dynamic_matchers_.end(); ++it) {
            ISrsHttpDynamicMatcher *matcher = *it;
            if ((err = matcher->dynamic_match(r, ph)) != srs_success) {
                return srs_error_wrap(err, "http dynamic match");
            }
        }
    }

    static ISrsHttpHandler *h404 = new SrsHttpNotFoundHandler();
    if (*ph == NULL) {
        *ph = h404;
    }

    return err;
}

srs_error_t SrsHttpServeMux::match(ISrsHttpMessage *r, ISrsHttpHandler **ph)
{
    std::string path = r->path();

    // Host-specific pattern takes precedence over generic ones
    if (!vhosts_.empty() && vhosts_.find(r->host()) != vhosts_.end()) {
        path = r->host() + path;
    }

    int nb_matched = 0;
    ISrsHttpHandler *h = NULL;

    std::map<std::string, SrsHttpMuxEntry *>::iterator it;
    for (it = static_matchers_.begin(); it != static_matchers_.end(); ++it) {
        std::string pattern = it->first;
        SrsHttpMuxEntry *entry = it->second;

        if (!entry->enabled) {
            continue;
        }

        if (!path_match(pattern, path)) {
            continue;
        }

        if (!h || (int)pattern.length() > nb_matched) {
            nb_matched = (int)pattern.length();
            h = entry->handler;
        }
    }

    *ph = h;

    return srs_success;
}

bool SrsHttpServeMux::path_match(string pattern, string path)
{
    if (pattern.empty()) {
        return false;
    }

    int n = (int)pattern.length();

    // not endswith '/', exactly match.
    if (pattern.at(n - 1) != '/') {
        return pattern == path;
    }

    // endswith '/', match any,
    // for example, '/api/' match '/api/[N]'
    if ((int)path.length() >= n) {
        if (memcmp(pattern.data(), path.data(), n) == 0) {
            return true;
        }
    }

    return false;
}

SrsHttpCorsMux::SrsHttpCorsMux(ISrsHttpHandler *h)
{
    enabled = false;
    required = false;
    next_ = h;
}

SrsHttpCorsMux::~SrsHttpCorsMux()
{
}

srs_error_t SrsHttpCorsMux::initialize(bool cros_enabled)
{
    enabled = cros_enabled;
    return srs_success;
}

srs_error_t SrsHttpCorsMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    // If CORS enabled, and there is a "Origin" header, it's CORS.
    if (enabled) {
        SrsHttpHeader *h = r->header();
        required = !h->get("Origin").empty();
    }

    // When CORS required, set the CORS headers.
    if (required) {
        SrsHttpHeader *h = w->header();
        // SRS does not need cookie or credentials, so we disable CORS credentials, and use * for CORS origin,
        // headers, expose headers and methods.
        h->set("Access-Control-Allow-Origin", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Headers
        h->set("Access-Control-Allow-Headers", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Methods
        h->set("Access-Control-Allow-Methods", "*");
        // See https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Expose-Headers
        // Only the CORS-safelisted response headers are exposed by default. That is Cache-Control, Content-Language,
        // Content-Length, Content-Type, Expires, Last-Modified, Pragma.
        // See https://developer.mozilla.org/en-US/docs/Glossary/CORS-safelisted_response_header
        h->set("Access-Control-Expose-Headers", "*");
        // https://stackoverflow.com/a/24689738/17679565
        // https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Access-Control-Allow-Credentials
        h->set("Access-Control-Allow-Credentials", "false");
        // CORS header for private network access, starting in Chrome 104
        h->set("Access-Control-Request-Private-Network", "true");
    }

    // handle the http options.
    if (r->is_http_options()) {
        w->header()->set_content_length(0);
        if (enabled) {
            w->write_header(SRS_CONSTS_HTTP_OK);
        } else {
            w->write_header(SRS_CONSTS_HTTP_MethodNotAllowed);
        }
        return w->final_request();
    }

    return next_->serve_http(w, r);
}

SrsHttpAuthMux::SrsHttpAuthMux(ISrsHttpHandler *h)
{
    next_ = h;
    enabled_ = false;
}

SrsHttpAuthMux::~SrsHttpAuthMux()
{
}

srs_error_t SrsHttpAuthMux::initialize(bool enabled, std::string username, std::string password)
{
    enabled_ = enabled;
    username_ = username;
    password_ = password;

    return srs_success;
}

srs_error_t SrsHttpAuthMux::serve_http(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err;
    if ((err = do_auth(w, r)) != srs_success) {
        srs_error("do_auth %s", srs_error_desc(err).c_str());
        srs_freep(err);
        w->write_header(SRS_CONSTS_HTTP_Unauthorized);
        return w->final_request();
    }

    srs_assert(next_);
    return next_->serve_http(w, r);
}

srs_error_t SrsHttpAuthMux::do_auth(ISrsHttpResponseWriter *w, ISrsHttpMessage *r)
{
    srs_error_t err = srs_success;

    if (!enabled_) {
        return err;
    }

    // We only apply for api starts with /api/ for HTTP API.
    // We don't apply for other apis such as /rtc/, for which we use http callback.
    if (r->path().find("/api/") == std::string::npos) {
        return err;
    }

    std::string auth = r->header()->get("Authorization");
    if (auth.empty()) {
        w->header()->set("WWW-Authenticate", SRS_HTTP_AUTH_SCHEME_BASIC);
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "empty Authorization");
    }

    if (!srs_strings_contains(auth, SRS_HTTP_AUTH_PREFIX_BASIC)) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid auth %s, should start with %s", auth.c_str(), SRS_HTTP_AUTH_PREFIX_BASIC);
    }

    std::string token = srs_erase_first_substr(auth, SRS_HTTP_AUTH_PREFIX_BASIC);
    if (token.empty()) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "empty token from auth %s", auth.c_str());
    }

    std::string plaintext;
    if ((err = srs_av_base64_decode(token, plaintext)) != srs_success) {
        return srs_error_wrap(err, "decode token %s", token.c_str());
    }

    // The token format must be username:password
    std::vector<std::string> user_pwd = srs_strings_split(plaintext, ":");
    if (user_pwd.size() != 2) {
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid token %s", plaintext.c_str());
    }

    if (username_ != user_pwd[0] || password_ != user_pwd[1]) {
        w->header()->set("WWW-Authenticate", SRS_HTTP_AUTH_SCHEME_BASIC);
        return srs_error_new(SRS_CONSTS_HTTP_Unauthorized, "invalid token %s:%s", user_pwd[0].c_str(), user_pwd[1].c_str());
    }

    return err;
}

ISrsHttpMessage::ISrsHttpMessage()
{
}

ISrsHttpMessage::~ISrsHttpMessage()
{
}

SrsHttpUri::SrsHttpUri()
{
    port = 0;
}

SrsHttpUri::~SrsHttpUri()
{
}

srs_error_t SrsHttpUri::initialize(string url)
{
    schema = host = path = query = fragment_ = "";
    url_ = url;

    // Replace the default vhost to a domain like string, or parse failed.
    string parsing_url = url;
    size_t pos_default_vhost = url.find("://__defaultVhost__");
    if (pos_default_vhost != string::npos) {
        parsing_url = srs_strings_replace(parsing_url, "://__defaultVhost__", "://safe.vhost.default.ossrs.io");
    }

    http_parser_url hp_u;
    http_parser_url_init(&hp_u);

    int r0;
    if ((r0 = http_parser_parse_url(parsing_url.c_str(), parsing_url.length(), 0, &hp_u)) != 0) {
        return srs_error_new(ERROR_HTTP_PARSE_URI, "parse url %s as %s failed, code=%d", url.c_str(), parsing_url.c_str(), r0);
    }

    std::string field = get_uri_field(parsing_url, &hp_u, UF_SCHEMA);
    if (!field.empty()) {
        schema = field;
    }

    // Restore the default vhost.
    if (pos_default_vhost == string::npos) {
        host = get_uri_field(parsing_url, &hp_u, UF_HOST);
    } else {
        host = SRS_CONSTS_RTMP_DEFAULT_VHOST;
    }

    field = get_uri_field(parsing_url, &hp_u, UF_PORT);
    if (!field.empty()) {
        port = ::atoi(field.c_str());
    }
    if (port <= 0) {
        if (schema == "https") {
            port = SRS_DEFAULT_HTTPS_PORT;
        } else if (schema == "rtmp") {
            port = SRS_CONSTS_RTMP_DEFAULT_PORT;
        } else if (schema == "redis") {
            port = SRS_DEFAULT_REDIS_PORT;
        } else {
            port = SRS_DEFAULT_HTTP_PORT;
        }
    }

    path = get_uri_field(parsing_url, &hp_u, UF_PATH);
    query = get_uri_field(parsing_url, &hp_u, UF_QUERY);
    fragment_ = get_uri_field(parsing_url, &hp_u, UF_FRAGMENT);

    username_ = get_uri_field(parsing_url, &hp_u, UF_USERINFO);
    size_t pos = username_.find(":");
    if (pos != string::npos) {
        password_ = username_.substr(pos + 1);
        username_ = username_.substr(0, pos);
    }

    return parse_query();
}

void SrsHttpUri::set_schema(std::string v)
{
    schema = v;

    // Update url with new schema.
    size_t pos = url_.find("://");
    if (pos != string::npos) {
        url_ = schema + "://" + url_.substr(pos + 3);
    }
}

string SrsHttpUri::get_url()
{
    return url_;
}

string SrsHttpUri::get_schema()
{
    return schema;
}

string SrsHttpUri::get_host()
{
    return host;
}

int SrsHttpUri::get_port()
{
    return port;
}

string SrsHttpUri::get_path()
{
    return path;
}

string SrsHttpUri::get_query()
{
    return query;
}

string SrsHttpUri::get_query_by_key(std::string key)
{
    map<string, string>::iterator it = query_values_.find(key);
    if (it == query_values_.end()) {
        return "";
    }
    return it->second;
}

std::string SrsHttpUri::get_fragment()
{
    return fragment_;
}

std::string SrsHttpUri::username()
{
    return username_;
}

std::string SrsHttpUri::password()
{
    return password_;
}

string SrsHttpUri::get_uri_field(const string &uri, void *php_u, int ifield)
{
    http_parser_url *hp_u = (http_parser_url *)php_u;
    http_parser_url_fields field = (http_parser_url_fields)ifield;

    if ((hp_u->field_set & (1 << field)) == 0) {
        return "";
    }

    int offset = hp_u->field_data[field].off;
    int len = hp_u->field_data[field].len;

    return uri.substr(offset, len);
}

srs_error_t SrsHttpUri::parse_query()
{
    srs_error_t err = srs_success;
    if (query.empty()) {
        return err;
    }

    size_t begin = query.find("?");
    if (string::npos != begin) {
        begin++;
    } else {
        begin = 0;
    }
    string query_str = query.substr(begin);
    query_values_.clear();
    srs_net_url_parse_query(query_str, query_values_);

    return err;
}

// @see golang net/url/url.go
namespace
{
enum EncodeMode {
    encodePath,
    encodePathSegment,
    encodeHost,
    encodeZone,
    encodeUserPassword,
    encodeQueryComponent,
    encodeFragment,
};

bool should_escape(uint8_t c, EncodeMode mode)
{
    // §2.3 Unreserved characters (alphanum)
    if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9')) {
        return false;
    }

    if (encodeHost == mode || encodeZone == mode) {
        // §3.2.2 Host allows
        //	sub-delims = "!" / "$" / "&" / "'" / "(" / ")" / "*" / "+" / "," / ";" / "="
        // as part of reg-name.
        // We add : because we include :port as part of host.
        // We add [ ] because we include [ipv6]:port as part of host.
        // We add < > because they're the only characters left that
        // we could possibly allow, and Parse will reject them if we
        // escape them (because hosts can't use %-encoding for
        // ASCII bytes).
        switch (c) {
        case '!':
        case '$':
        case '&':
        case '\'':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case ';':
        case '=':
        case ':':
        case '[':
        case ']':
        case '<':
        case '>':
        case '"':
            return false;
        }
    }

    switch (c) {
    case '-':
    case '_':
    case '.':
    case '~': // §2.3 Unreserved characters (mark)
        return false;
    case '$':
    case '&':
    case '+':
    case ',':
    case '/':
    case ':':
    case ';':
    case '=':
    case '?':
    case '@': // §2.2 Reserved characters (reserved)
        // Different sections of the URL allow a few of
        // the reserved characters to appear unescaped.
        switch (mode) {
        case encodePath: // §3.3
            // The RFC allows : @ & = + $ but saves / ; , for assigning
            // meaning to individual path segments. This package
            // only manipulates the path as a whole, so we allow those
            // last three as well. That leaves only ? to escape.
            return c == '?';

        case encodePathSegment: // §3.3
            // The RFC allows : @ & = + $ but saves / ; , for assigning
            // meaning to individual path segments.
            return c == '/' || c == ';' || c == ',' || c == '?';

        case encodeUserPassword: // §3.2.1
            // The RFC allows ';', ':', '&', '=', '+', '$', and ',' in
            // userinfo, so we must escape only '@', '/', and '?'.
            // The parsing of userinfo treats ':' as special so we must escape
            // that too.
            return c == '@' || c == '/' || c == '?' || c == ':';

        case encodeQueryComponent: // §3.4
            // The RFC reserves (so we must escape) everything.
            return true;

        case encodeFragment: // §4.1
            // The RFC text is silent but the grammar allows
            // everything, so escape nothing.
            return false;
        default:
            break;
        }
    }

    if (mode == encodeFragment) {
        // RFC 3986 §2.2 allows not escaping sub-delims. A subset of sub-delims are
        // included in reserved from RFC 2396 §2.2. The remaining sub-delims do not
        // need to be escaped. To minimize potential breakage, we apply two restrictions:
        // (1) we always escape sub-delims outside of the fragment, and (2) we always
        // escape single quote to avoid breaking callers that had previously assumed that
        // single quotes would be escaped. See issue #19917.
        switch (c) {
        case '!':
        case '(':
        case ')':
        case '*':
            return false;
        }
    }

    // Everything else must be escaped.
    return true;
}

bool ishex(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return true;
    } else if ('a' <= c && c <= 'f') {
        return true;
    } else if ('A' <= c && c <= 'F') {
        return true;
    }
    return false;
}

uint8_t hex_to_num(uint8_t c)
{
    if ('0' <= c && c <= '9') {
        return c - '0';
    } else if ('a' <= c && c <= 'f') {
        return c - 'a' + 10;
    } else if ('A' <= c && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

srs_error_t unescapse(string s, string &value, EncodeMode mode)
{
    srs_error_t err = srs_success;
    int n = 0;
    bool has_plus = false;
    int i = 0;
    // Count %, check that they're well-formed.
    while (i < (int)s.length()) {
        switch (s.at(i)) {
        case '%': {
            n++;
            if ((i + 2) >= (int)s.length() || !ishex(s.at(i + 1)) || !ishex(s.at(i + 2))) {
                string msg = s.substr(i);
                if (msg.length() > 3) {
                    msg = msg.substr(0, 3);
                }
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escape: %s", msg.c_str());
            }

            // Per https://tools.ietf.org/html/rfc3986#page-21
            // in the host component %-encoding can only be used
            // for non-ASCII bytes.
            // But https://tools.ietf.org/html/rfc6874#section-2
            // introduces %25 being allowed to escape a percent sign
            // in IPv6 scoped-address literals. Yay.
            if (encodeHost == mode && hex_to_num(s.at(i + 1)) < 8 && s.substr(i, 3) != "%25") {
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
            }

            if (encodeZone == mode) {
                // RFC 6874 says basically "anything goes" for zone identifiers
                // and that even non-ASCII can be redundantly escaped,
                // but it seems prudent to restrict %-escaped bytes here to those
                // that are valid host name bytes in their unescaped form.
                // That is, you can use escaping in the zone identifier but not
                // to introduce bytes you couldn't just write directly.
                // But Windows puts spaces here! Yay.
                uint8_t v = (hex_to_num(s.at(i + 1)) << 4) | (hex_to_num(s.at(i + 2)));
                if ("%25" != s.substr(i, 3) && ' ' != v && should_escape(v, encodeHost)) {
                    return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid URL escap: %s", s.substr(i, 3).c_str());
                }
            }
            i += 3;
        } break;
        case '+':
            has_plus = encodeQueryComponent == mode;
            i++;
            break;
        default:
            if ((encodeHost == mode || encodeZone == mode) && ((uint8_t)s.at(i) < 0x80) && should_escape(s.at(i), mode)) {
                return srs_error_new(ERROR_HTTP_URL_UNESCAPE, "invalid character %u in host name", s.at(i));
            }
            i++;
            break;
        }
    }

    if (0 == n && !has_plus) {
        value = s;
        return err;
    }

    value.clear();
    // value.resize(s.length() - 2*n);
    for (int i = 0; i < (int)s.length(); ++i) {
        switch (s.at(i)) {
        case '%':
            value += (hex_to_num(s.at(i + 1)) << 4 | hex_to_num(s.at(i + 2)));
            i += 2;
            break;
        case '+':
            if (encodeQueryComponent == mode) {
                value += " ";
            } else {
                value += "+";
            }
            break;
        default:
            value += s.at(i);
            break;
        }
    }

    return srs_success;
}

string escape(string s, EncodeMode mode)
{
    int space_count = 0;
    int hex_count = 0;
    for (int i = 0; i < (int)s.length(); ++i) {
        uint8_t c = s.at(i);
        if (should_escape(c, mode)) {
            if (' ' == c && encodeQueryComponent == mode) {
                space_count++;
            } else {
                hex_count++;
            }
        }
    }

    if (0 == space_count && 0 == hex_count) {
        return s;
    }

    string value;
    if (0 == hex_count) {
        value = s;
        for (int i = 0; i < (int)s.length(); ++i) {
            if (' ' == s.at(i)) {
                value[i] = '+';
            }
        }
        return value;
    }

    // value.resize(s.length() + 2*hex_count);
    const char escape_code[] = "0123456789ABCDEF";
    // int j = 0;
    for (int i = 0; i < (int)s.length(); ++i) {
        uint8_t c = s.at(i);
        if (' ' == c && encodeQueryComponent == mode) {
            value += '+';
        } else if (should_escape(c, mode)) {
            value += '%';
            value += escape_code[c >> 4];
            value += escape_code[c & 15];
            // j += 3;
        } else {
            value += s[i];
        }
    }

    return value;
}

} // namespace

string SrsHttpUri::query_escape(std::string s)
{
    return escape(s, encodeQueryComponent);
}

string SrsHttpUri::path_escape(std::string s)
{
    return escape(s, encodePathSegment);
}

srs_error_t SrsHttpUri::query_unescape(std::string s, std::string &value)
{
    return unescapse(s, value, encodeQueryComponent);
}

srs_error_t SrsHttpUri::path_unescape(std::string s, std::string &value)
{
    return unescapse(s, value, encodePathSegment);
}

// LCOV_EXCL_START

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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SSE4_2__
#ifdef _MSC_VER
#include <nmmintrin.h>
#else /* !_MSC_VER */
#include <x86intrin.h>
#endif /* _MSC_VER */
#endif /* __SSE4_2__ */

#ifdef __ARM_NEON__
#include <arm_neon.h>
#endif /* __ARM_NEON__ */

#ifdef __wasm__
#include <wasm_simd128.h>
#endif /* __wasm__ */

#ifdef _MSC_VER
#define ALIGN(n) _declspec(align(n))
#define UNREACHABLE __assume(0)
#else /* !_MSC_VER */
#define ALIGN(n) __attribute__((aligned(n)))
#define UNREACHABLE __builtin_unreachable()
#endif /* _MSC_VER */

#include "llhttp.h"

typedef int (*llhttp__internal__span_cb)(
    llhttp__internal_t *, const char *, const char *);

static const unsigned char llparse_blob0[] = {
    'o', 'n'};
static const unsigned char llparse_blob1[] = {
    'e', 'c', 't', 'i', 'o', 'n'};
static const unsigned char llparse_blob2[] = {
    'l', 'o', 's', 'e'};
static const unsigned char llparse_blob3[] = {
    'e', 'e', 'p', '-', 'a', 'l', 'i', 'v', 'e'};
static const unsigned char llparse_blob4[] = {
    'p', 'g', 'r', 'a', 'd', 'e'};
static const unsigned char llparse_blob5[] = {
    'c', 'h', 'u', 'n', 'k', 'e', 'd'};
#ifdef __SSE4_2__
static const unsigned char ALIGN(16) llparse_blob6[] = {
    0x9, 0x9, ' ', '~', 0x80, 0xff, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0};
#endif /* __SSE4_2__ */
#ifdef __SSE4_2__
static const unsigned char ALIGN(16) llparse_blob7[] = {
    '!', '!', '#', '\'', '*', '+', '-', '.', '0', '9', 'A',
    'Z', '^', 'z', '|', '|'};
#endif /* __SSE4_2__ */
#ifdef __SSE4_2__
static const unsigned char ALIGN(16) llparse_blob8[] = {
    '~', '~', 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0};
#endif /* __SSE4_2__ */
static const unsigned char llparse_blob9[] = {
    'e', 'n', 't', '-', 'l', 'e', 'n', 'g', 't', 'h'};
static const unsigned char llparse_blob10[] = {
    'r', 'o', 'x', 'y', '-', 'c', 'o', 'n', 'n', 'e', 'c',
    't', 'i', 'o', 'n'};
static const unsigned char llparse_blob11[] = {
    'r', 'a', 'n', 's', 'f', 'e', 'r', '-', 'e', 'n', 'c',
    'o', 'd', 'i', 'n', 'g'};
static const unsigned char llparse_blob12[] = {
    'p', 'g', 'r', 'a', 'd', 'e'};
static const unsigned char llparse_blob13[] = {
    'T', 'T', 'P'};
static const unsigned char llparse_blob14[] = {
    0xd, 0xa, 0xd, 0xa, 'S', 'M', 0xd, 0xa, 0xd, 0xa};
static const unsigned char llparse_blob15[] = {
    'C', 'E'};
static const unsigned char llparse_blob16[] = {
    'T', 'S', 'P'};
static const unsigned char llparse_blob17[] = {
    'N', 'O', 'U', 'N', 'C', 'E'};
static const unsigned char llparse_blob18[] = {
    'I', 'N', 'D'};
static const unsigned char llparse_blob19[] = {
    'E', 'C', 'K', 'O', 'U', 'T'};
static const unsigned char llparse_blob20[] = {
    'N', 'E', 'C', 'T'};
static const unsigned char llparse_blob21[] = {
    'E', 'T', 'E'};
static const unsigned char llparse_blob22[] = {
    'C', 'R', 'I', 'B', 'E'};
static const unsigned char llparse_blob23[] = {
    'L', 'U', 'S', 'H'};
static const unsigned char llparse_blob24[] = {
    'E', 'T'};
static const unsigned char llparse_blob25[] = {
    'P', 'A', 'R', 'A', 'M', 'E', 'T', 'E', 'R'};
static const unsigned char llparse_blob26[] = {
    'E', 'A', 'D'};
static const unsigned char llparse_blob27[] = {
    'N', 'K'};
static const unsigned char llparse_blob28[] = {
    'C', 'K'};
static const unsigned char llparse_blob29[] = {
    'S', 'E', 'A', 'R', 'C', 'H'};
static const unsigned char llparse_blob30[] = {
    'R', 'G', 'E'};
static const unsigned char llparse_blob31[] = {
    'C', 'T', 'I', 'V', 'I', 'T', 'Y'};
static const unsigned char llparse_blob32[] = {
    'L', 'E', 'N', 'D', 'A', 'R'};
static const unsigned char llparse_blob33[] = {
    'V', 'E'};
static const unsigned char llparse_blob34[] = {
    'O', 'T', 'I', 'F', 'Y'};
static const unsigned char llparse_blob35[] = {
    'P', 'T', 'I', 'O', 'N', 'S'};
static const unsigned char llparse_blob36[] = {
    'C', 'H'};
static const unsigned char llparse_blob37[] = {
    'S', 'E'};
static const unsigned char llparse_blob38[] = {
    'A', 'Y'};
static const unsigned char llparse_blob39[] = {
    'S', 'T'};
static const unsigned char llparse_blob40[] = {
    'I', 'N', 'D'};
static const unsigned char llparse_blob41[] = {
    'A', 'T', 'C', 'H'};
static const unsigned char llparse_blob42[] = {
    'G', 'E'};
static const unsigned char llparse_blob43[] = {
    'U', 'E', 'R', 'Y'};
static const unsigned char llparse_blob44[] = {
    'I', 'N', 'D'};
static const unsigned char llparse_blob45[] = {
    'O', 'R', 'D'};
static const unsigned char llparse_blob46[] = {
    'I', 'R', 'E', 'C', 'T'};
static const unsigned char llparse_blob47[] = {
    'O', 'R', 'T'};
static const unsigned char llparse_blob48[] = {
    'R', 'C', 'H'};
static const unsigned char llparse_blob49[] = {
    'P', 'A', 'R', 'A', 'M', 'E', 'T', 'E', 'R'};
static const unsigned char llparse_blob50[] = {
    'U', 'R', 'C', 'E'};
static const unsigned char llparse_blob51[] = {
    'B', 'S', 'C', 'R', 'I', 'B', 'E'};
static const unsigned char llparse_blob52[] = {
    'A', 'R', 'D', 'O', 'W', 'N'};
static const unsigned char llparse_blob53[] = {
    'A', 'C', 'E'};
static const unsigned char llparse_blob54[] = {
    'I', 'N', 'D'};
static const unsigned char llparse_blob55[] = {
    'N', 'K'};
static const unsigned char llparse_blob56[] = {
    'C', 'K'};
static const unsigned char llparse_blob57[] = {
    'U', 'B', 'S', 'C', 'R', 'I', 'B', 'E'};
static const unsigned char llparse_blob58[] = {
    'T', 'T', 'P'};
static const unsigned char llparse_blob59[] = {
    'C', 'E'};
static const unsigned char llparse_blob60[] = {
    'T', 'S', 'P'};
static const unsigned char llparse_blob61[] = {
    'A', 'D'};
static const unsigned char llparse_blob62[] = {
    'T', 'P', '/'};

enum llparse_match_status_e {
    kMatchComplete,
    kMatchPause,
    kMatchMismatch
};
typedef enum llparse_match_status_e llparse_match_status_t;

struct llparse_match_s {
    llparse_match_status_t status;
    const unsigned char *current;
};
typedef struct llparse_match_s llparse_match_t;

static llparse_match_t llparse__match_sequence_to_lower(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp,
    const unsigned char *seq, uint32_t seq_len)
{
    uint32_t index;
    llparse_match_t res;

    index = s->_index;
    for (; p != endp; p++) {
        unsigned char current;

        current = ((*p) >= 'A' && (*p) <= 'Z' ? (*p | 0x20) : (*p));
        if (current == seq[index]) {
            if (++index == seq_len) {
                res.status = kMatchComplete;
                goto reset;
            }
        } else {
            res.status = kMatchMismatch;
            goto reset;
        }
    }
    s->_index = index;
    res.status = kMatchPause;
    res.current = p;
    return res;
reset:
    s->_index = 0;
    res.current = p;
    return res;
}

static llparse_match_t llparse__match_sequence_to_lower_unsafe(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp,
    const unsigned char *seq, uint32_t seq_len)
{
    uint32_t index;
    llparse_match_t res;

    index = s->_index;
    for (; p != endp; p++) {
        unsigned char current;

        current = ((*p) | 0x20);
        if (current == seq[index]) {
            if (++index == seq_len) {
                res.status = kMatchComplete;
                goto reset;
            }
        } else {
            res.status = kMatchMismatch;
            goto reset;
        }
    }
    s->_index = index;
    res.status = kMatchPause;
    res.current = p;
    return res;
reset:
    s->_index = 0;
    res.current = p;
    return res;
}

static llparse_match_t llparse__match_sequence_id(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp,
    const unsigned char *seq, uint32_t seq_len)
{
    uint32_t index;
    llparse_match_t res;

    index = s->_index;
    for (; p != endp; p++) {
        unsigned char current;

        current = *p;
        if (current == seq[index]) {
            if (++index == seq_len) {
                res.status = kMatchComplete;
                goto reset;
            }
        } else {
            res.status = kMatchMismatch;
            goto reset;
        }
    }
    s->_index = index;
    res.status = kMatchPause;
    res.current = p;
    return res;
reset:
    s->_index = 0;
    res.current = p;
    return res;
}

enum llparse_state_e {
    s_error,
    s_n_llhttp__internal__n_closed,
    s_n_llhttp__internal__n_invoke_llhttp__after_message_complete,
    s_n_llhttp__internal__n_pause_1,
    s_n_llhttp__internal__n_invoke_is_equal_upgrade,
    s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2,
    s_n_llhttp__internal__n_chunk_data_almost_done_1,
    s_n_llhttp__internal__n_chunk_data_almost_done,
    s_n_llhttp__internal__n_consume_content_length,
    s_n_llhttp__internal__n_span_start_llhttp__on_body,
    s_n_llhttp__internal__n_invoke_is_equal_content_length,
    s_n_llhttp__internal__n_chunk_size_almost_done,
    s_n_llhttp__internal__n_invoke_test_lenient_flags_9,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_1,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_2,
    s_n_llhttp__internal__n_invoke_test_lenient_flags_10,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_1,
    s_n_llhttp__internal__n_chunk_extension_quoted_value_done,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_2,
    s_n_llhttp__internal__n_error_30,
    s_n_llhttp__internal__n_chunk_extension_quoted_value_quoted_pair,
    s_n_llhttp__internal__n_error_31,
    s_n_llhttp__internal__n_chunk_extension_quoted_value,
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_3,
    s_n_llhttp__internal__n_error_33,
    s_n_llhttp__internal__n_chunk_extension_value,
    s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value,
    s_n_llhttp__internal__n_error_34,
    s_n_llhttp__internal__n_chunk_extension_name,
    s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_name,
    s_n_llhttp__internal__n_chunk_extensions,
    s_n_llhttp__internal__n_chunk_size_otherwise,
    s_n_llhttp__internal__n_chunk_size,
    s_n_llhttp__internal__n_chunk_size_digit,
    s_n_llhttp__internal__n_invoke_update_content_length_1,
    s_n_llhttp__internal__n_consume_content_length_1,
    s_n_llhttp__internal__n_span_start_llhttp__on_body_1,
    s_n_llhttp__internal__n_eof,
    s_n_llhttp__internal__n_span_start_llhttp__on_body_2,
    s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete,
    s_n_llhttp__internal__n_error_5,
    s_n_llhttp__internal__n_headers_almost_done,
    s_n_llhttp__internal__n_header_field_colon_discard_ws,
    s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete,
    s_n_llhttp__internal__n_span_start_llhttp__on_header_value,
    s_n_llhttp__internal__n_header_value_discard_lws,
    s_n_llhttp__internal__n_header_value_discard_ws_almost_done,
    s_n_llhttp__internal__n_header_value_lws,
    s_n_llhttp__internal__n_header_value_almost_done,
    s_n_llhttp__internal__n_invoke_test_lenient_flags_17,
    s_n_llhttp__internal__n_header_value_lenient,
    s_n_llhttp__internal__n_error_54,
    s_n_llhttp__internal__n_header_value_otherwise,
    s_n_llhttp__internal__n_header_value_connection_token,
    s_n_llhttp__internal__n_header_value_connection_ws,
    s_n_llhttp__internal__n_header_value_connection_1,
    s_n_llhttp__internal__n_header_value_connection_2,
    s_n_llhttp__internal__n_header_value_connection_3,
    s_n_llhttp__internal__n_header_value_connection,
    s_n_llhttp__internal__n_error_56,
    s_n_llhttp__internal__n_error_57,
    s_n_llhttp__internal__n_header_value_content_length_ws,
    s_n_llhttp__internal__n_header_value_content_length,
    s_n_llhttp__internal__n_error_59,
    s_n_llhttp__internal__n_error_58,
    s_n_llhttp__internal__n_header_value_te_token_ows,
    s_n_llhttp__internal__n_header_value,
    s_n_llhttp__internal__n_header_value_te_token,
    s_n_llhttp__internal__n_header_value_te_chunked_last,
    s_n_llhttp__internal__n_header_value_te_chunked,
    s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1,
    s_n_llhttp__internal__n_header_value_discard_ws,
    s_n_llhttp__internal__n_invoke_load_header_state,
    s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete,
    s_n_llhttp__internal__n_header_field_general_otherwise,
    s_n_llhttp__internal__n_header_field_general,
    s_n_llhttp__internal__n_header_field_colon,
    s_n_llhttp__internal__n_header_field_3,
    s_n_llhttp__internal__n_header_field_4,
    s_n_llhttp__internal__n_header_field_2,
    s_n_llhttp__internal__n_header_field_1,
    s_n_llhttp__internal__n_header_field_5,
    s_n_llhttp__internal__n_header_field_6,
    s_n_llhttp__internal__n_header_field_7,
    s_n_llhttp__internal__n_header_field,
    s_n_llhttp__internal__n_span_start_llhttp__on_header_field,
    s_n_llhttp__internal__n_header_field_start,
    s_n_llhttp__internal__n_headers_start,
    s_n_llhttp__internal__n_url_to_http_09,
    s_n_llhttp__internal__n_url_skip_to_http09,
    s_n_llhttp__internal__n_url_skip_lf_to_http09_1,
    s_n_llhttp__internal__n_url_skip_lf_to_http09,
    s_n_llhttp__internal__n_req_pri_upgrade,
    s_n_llhttp__internal__n_req_http_complete_crlf,
    s_n_llhttp__internal__n_req_http_complete,
    s_n_llhttp__internal__n_invoke_load_method_1,
    s_n_llhttp__internal__n_invoke_llhttp__on_version_complete,
    s_n_llhttp__internal__n_error_67,
    s_n_llhttp__internal__n_error_74,
    s_n_llhttp__internal__n_req_http_minor,
    s_n_llhttp__internal__n_error_75,
    s_n_llhttp__internal__n_req_http_dot,
    s_n_llhttp__internal__n_error_76,
    s_n_llhttp__internal__n_req_http_major,
    s_n_llhttp__internal__n_span_start_llhttp__on_version,
    s_n_llhttp__internal__n_req_after_protocol,
    s_n_llhttp__internal__n_invoke_load_method,
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete,
    s_n_llhttp__internal__n_error_82,
    s_n_llhttp__internal__n_req_after_http_start_1,
    s_n_llhttp__internal__n_invoke_load_method_2,
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_1,
    s_n_llhttp__internal__n_req_after_http_start_2,
    s_n_llhttp__internal__n_invoke_load_method_3,
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_2,
    s_n_llhttp__internal__n_req_after_http_start_3,
    s_n_llhttp__internal__n_req_after_http_start,
    s_n_llhttp__internal__n_span_start_llhttp__on_protocol,
    s_n_llhttp__internal__n_req_http_start,
    s_n_llhttp__internal__n_url_to_http,
    s_n_llhttp__internal__n_url_skip_to_http,
    s_n_llhttp__internal__n_url_fragment,
    s_n_llhttp__internal__n_span_end_stub_query_3,
    s_n_llhttp__internal__n_url_query,
    s_n_llhttp__internal__n_url_query_or_fragment,
    s_n_llhttp__internal__n_url_path,
    s_n_llhttp__internal__n_span_start_stub_path_2,
    s_n_llhttp__internal__n_span_start_stub_path,
    s_n_llhttp__internal__n_span_start_stub_path_1,
    s_n_llhttp__internal__n_url_server_with_at,
    s_n_llhttp__internal__n_url_server,
    s_n_llhttp__internal__n_url_schema_delim_1,
    s_n_llhttp__internal__n_url_schema_delim,
    s_n_llhttp__internal__n_span_end_stub_schema,
    s_n_llhttp__internal__n_url_schema,
    s_n_llhttp__internal__n_url_start,
    s_n_llhttp__internal__n_span_start_llhttp__on_url_1,
    s_n_llhttp__internal__n_url_entry_normal,
    s_n_llhttp__internal__n_span_start_llhttp__on_url,
    s_n_llhttp__internal__n_url_entry_connect,
    s_n_llhttp__internal__n_req_spaces_before_url,
    s_n_llhttp__internal__n_req_first_space_before_url,
    s_n_llhttp__internal__n_invoke_llhttp__on_method_complete_1,
    s_n_llhttp__internal__n_after_start_req_2,
    s_n_llhttp__internal__n_after_start_req_3,
    s_n_llhttp__internal__n_after_start_req_1,
    s_n_llhttp__internal__n_after_start_req_4,
    s_n_llhttp__internal__n_after_start_req_6,
    s_n_llhttp__internal__n_after_start_req_8,
    s_n_llhttp__internal__n_after_start_req_9,
    s_n_llhttp__internal__n_after_start_req_7,
    s_n_llhttp__internal__n_after_start_req_5,
    s_n_llhttp__internal__n_after_start_req_12,
    s_n_llhttp__internal__n_after_start_req_13,
    s_n_llhttp__internal__n_after_start_req_11,
    s_n_llhttp__internal__n_after_start_req_10,
    s_n_llhttp__internal__n_after_start_req_14,
    s_n_llhttp__internal__n_after_start_req_17,
    s_n_llhttp__internal__n_after_start_req_16,
    s_n_llhttp__internal__n_after_start_req_15,
    s_n_llhttp__internal__n_after_start_req_18,
    s_n_llhttp__internal__n_after_start_req_20,
    s_n_llhttp__internal__n_after_start_req_21,
    s_n_llhttp__internal__n_after_start_req_19,
    s_n_llhttp__internal__n_after_start_req_23,
    s_n_llhttp__internal__n_after_start_req_24,
    s_n_llhttp__internal__n_after_start_req_26,
    s_n_llhttp__internal__n_after_start_req_28,
    s_n_llhttp__internal__n_after_start_req_29,
    s_n_llhttp__internal__n_after_start_req_27,
    s_n_llhttp__internal__n_after_start_req_25,
    s_n_llhttp__internal__n_after_start_req_30,
    s_n_llhttp__internal__n_after_start_req_22,
    s_n_llhttp__internal__n_after_start_req_31,
    s_n_llhttp__internal__n_after_start_req_32,
    s_n_llhttp__internal__n_after_start_req_35,
    s_n_llhttp__internal__n_after_start_req_36,
    s_n_llhttp__internal__n_after_start_req_34,
    s_n_llhttp__internal__n_after_start_req_37,
    s_n_llhttp__internal__n_after_start_req_38,
    s_n_llhttp__internal__n_after_start_req_42,
    s_n_llhttp__internal__n_after_start_req_43,
    s_n_llhttp__internal__n_after_start_req_41,
    s_n_llhttp__internal__n_after_start_req_40,
    s_n_llhttp__internal__n_after_start_req_39,
    s_n_llhttp__internal__n_after_start_req_45,
    s_n_llhttp__internal__n_after_start_req_44,
    s_n_llhttp__internal__n_after_start_req_33,
    s_n_llhttp__internal__n_after_start_req_46,
    s_n_llhttp__internal__n_after_start_req_49,
    s_n_llhttp__internal__n_after_start_req_50,
    s_n_llhttp__internal__n_after_start_req_51,
    s_n_llhttp__internal__n_after_start_req_52,
    s_n_llhttp__internal__n_after_start_req_48,
    s_n_llhttp__internal__n_after_start_req_47,
    s_n_llhttp__internal__n_after_start_req_55,
    s_n_llhttp__internal__n_after_start_req_57,
    s_n_llhttp__internal__n_after_start_req_58,
    s_n_llhttp__internal__n_after_start_req_56,
    s_n_llhttp__internal__n_after_start_req_54,
    s_n_llhttp__internal__n_after_start_req_59,
    s_n_llhttp__internal__n_after_start_req_60,
    s_n_llhttp__internal__n_after_start_req_53,
    s_n_llhttp__internal__n_after_start_req_62,
    s_n_llhttp__internal__n_after_start_req_63,
    s_n_llhttp__internal__n_after_start_req_61,
    s_n_llhttp__internal__n_after_start_req_66,
    s_n_llhttp__internal__n_after_start_req_68,
    s_n_llhttp__internal__n_after_start_req_69,
    s_n_llhttp__internal__n_after_start_req_67,
    s_n_llhttp__internal__n_after_start_req_70,
    s_n_llhttp__internal__n_after_start_req_65,
    s_n_llhttp__internal__n_after_start_req_64,
    s_n_llhttp__internal__n_after_start_req,
    s_n_llhttp__internal__n_span_start_llhttp__on_method_1,
    s_n_llhttp__internal__n_res_line_almost_done,
    s_n_llhttp__internal__n_invoke_test_lenient_flags_30,
    s_n_llhttp__internal__n_res_status,
    s_n_llhttp__internal__n_span_start_llhttp__on_status,
    s_n_llhttp__internal__n_res_status_code_otherwise,
    s_n_llhttp__internal__n_res_status_code_digit_3,
    s_n_llhttp__internal__n_res_status_code_digit_2,
    s_n_llhttp__internal__n_res_status_code_digit_1,
    s_n_llhttp__internal__n_res_after_version,
    s_n_llhttp__internal__n_invoke_llhttp__on_version_complete_1,
    s_n_llhttp__internal__n_error_93,
    s_n_llhttp__internal__n_error_107,
    s_n_llhttp__internal__n_res_http_minor,
    s_n_llhttp__internal__n_error_108,
    s_n_llhttp__internal__n_res_http_dot,
    s_n_llhttp__internal__n_error_109,
    s_n_llhttp__internal__n_res_http_major,
    s_n_llhttp__internal__n_span_start_llhttp__on_version_1,
    s_n_llhttp__internal__n_res_after_protocol,
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_3,
    s_n_llhttp__internal__n_error_115,
    s_n_llhttp__internal__n_res_after_start_1,
    s_n_llhttp__internal__n_res_after_start_2,
    s_n_llhttp__internal__n_res_after_start_3,
    s_n_llhttp__internal__n_res_after_start,
    s_n_llhttp__internal__n_span_start_llhttp__on_protocol_1,
    s_n_llhttp__internal__n_invoke_llhttp__on_method_complete,
    s_n_llhttp__internal__n_req_or_res_method_2,
    s_n_llhttp__internal__n_invoke_update_type_1,
    s_n_llhttp__internal__n_req_or_res_method_3,
    s_n_llhttp__internal__n_req_or_res_method_1,
    s_n_llhttp__internal__n_req_or_res_method,
    s_n_llhttp__internal__n_span_start_llhttp__on_method,
    s_n_llhttp__internal__n_start_req_or_res,
    s_n_llhttp__internal__n_invoke_load_type,
    s_n_llhttp__internal__n_invoke_update_finish,
    s_n_llhttp__internal__n_start,
};
typedef enum llparse_state_e llparse_state_t;

int llhttp__on_method(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_url(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_protocol(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_version(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_header_field(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_header_value(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_body(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_chunk_extension_name(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_chunk_extension_value(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_status(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_load_initial_message_completed(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->initial_message_completed;
}

int llhttp__on_reset(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_update_finish(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->finish = 2;
    return 0;
}

int llhttp__on_message_begin(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_load_type(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->type;
}

int llhttp__internal__c_store_method(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    state->method = match;
    return 0;
}

int llhttp__on_method_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_is_equal_method(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->method == 5;
}

int llhttp__internal__c_update_http_major(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->http_major = 0;
    return 0;
}

int llhttp__internal__c_update_http_minor(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->http_minor = 9;
    return 0;
}

int llhttp__on_url_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_test_lenient_flags(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 1) == 1;
}

int llhttp__internal__c_test_lenient_flags_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 256) == 256;
}

int llhttp__internal__c_test_flags(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->flags & 128) == 128;
}

int llhttp__on_chunk_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_message_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_is_equal_upgrade(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->upgrade == 1;
}

int llhttp__after_message_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_update_content_length(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->content_length = 0;
    return 0;
}

int llhttp__internal__c_update_initial_message_completed(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->initial_message_completed = 1;
    return 0;
}

int llhttp__internal__c_update_finish_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->finish = 0;
    return 0;
}

int llhttp__internal__c_test_lenient_flags_2(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 4) == 4;
}

int llhttp__internal__c_test_lenient_flags_3(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 32) == 32;
}

int llhttp__before_headers_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_headers_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__after_headers_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_mul_add_content_length(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    /* Multiplication overflow */
    if (state->content_length > 0xffffffffffffffffULL / 16) {
        return 1;
    }

    state->content_length *= 16;

    /* Addition overflow */
    if (match >= 0) {
        if (state->content_length > 0xffffffffffffffffULL - match) {
            return 1;
        }
    } else {
        if (state->content_length < 0ULL - match) {
            return 1;
        }
    }
    state->content_length += match;
    return 0;
}

int llhttp__internal__c_test_lenient_flags_4(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 512) == 512;
}

int llhttp__on_chunk_header(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_is_equal_content_length(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->content_length == 0;
}

int llhttp__internal__c_test_lenient_flags_7(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 128) == 128;
}

int llhttp__internal__c_or_flags(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 128;
    return 0;
}

int llhttp__internal__c_test_lenient_flags_8(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 64) == 64;
}

int llhttp__on_chunk_extension_name_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__on_chunk_extension_value_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_update_finish_3(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->finish = 1;
    return 0;
}

int llhttp__internal__c_or_flags_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 64;
    return 0;
}

int llhttp__internal__c_update_upgrade(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->upgrade = 1;
    return 0;
}

int llhttp__internal__c_store_header_state(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    state->header_state = match;
    return 0;
}

int llhttp__on_header_field_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_load_header_state(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->header_state;
}

int llhttp__internal__c_test_flags_4(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->flags & 512) == 512;
}

int llhttp__internal__c_test_lenient_flags_22(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 2) == 2;
}

int llhttp__internal__c_or_flags_5(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 1;
    return 0;
}

int llhttp__internal__c_update_header_state(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 1;
    return 0;
}

int llhttp__on_header_value_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_or_flags_6(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 2;
    return 0;
}

int llhttp__internal__c_or_flags_7(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 4;
    return 0;
}

int llhttp__internal__c_or_flags_8(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 8;
    return 0;
}

int llhttp__internal__c_update_header_state_3(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 6;
    return 0;
}

int llhttp__internal__c_update_header_state_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 0;
    return 0;
}

int llhttp__internal__c_update_header_state_6(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 5;
    return 0;
}

int llhttp__internal__c_update_header_state_7(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 7;
    return 0;
}

int llhttp__internal__c_test_flags_2(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->flags & 32) == 32;
}

int llhttp__internal__c_mul_add_content_length_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    /* Multiplication overflow */
    if (state->content_length > 0xffffffffffffffffULL / 10) {
        return 1;
    }

    state->content_length *= 10;

    /* Addition overflow */
    if (match >= 0) {
        if (state->content_length > 0xffffffffffffffffULL - match) {
            return 1;
        }
    } else {
        if (state->content_length < 0ULL - match) {
            return 1;
        }
    }
    state->content_length += match;
    return 0;
}

int llhttp__internal__c_or_flags_17(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 32;
    return 0;
}

int llhttp__internal__c_test_flags_3(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->flags & 8) == 8;
}

int llhttp__internal__c_test_lenient_flags_20(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 8) == 8;
}

int llhttp__internal__c_or_flags_18(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 512;
    return 0;
}

int llhttp__internal__c_and_flags(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags &= -9;
    return 0;
}

int llhttp__internal__c_update_header_state_8(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->header_state = 8;
    return 0;
}

int llhttp__internal__c_or_flags_20(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->flags |= 16;
    return 0;
}

int llhttp__on_protocol_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_load_method(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->method;
}

int llhttp__internal__c_store_http_major(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    state->http_major = match;
    return 0;
}

int llhttp__internal__c_store_http_minor(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    state->http_minor = match;
    return 0;
}

int llhttp__internal__c_test_lenient_flags_24(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return (state->lenient_flags & 16) == 16;
}

int llhttp__on_version_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_load_http_major(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->http_major;
}

int llhttp__internal__c_load_http_minor(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    return state->http_minor;
}

int llhttp__internal__c_update_status_code(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->status_code = 0;
    return 0;
}

int llhttp__internal__c_mul_add_status_code(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp,
    int match)
{
    /* Multiplication overflow */
    if (state->status_code > 0xffff / 10) {
        return 1;
    }

    state->status_code *= 10;

    /* Addition overflow */
    if (match >= 0) {
        if (state->status_code > 0xffff - match) {
            return 1;
        }
    } else {
        if (state->status_code < 0 - match) {
            return 1;
        }
    }
    state->status_code += match;
    return 0;
}

int llhttp__on_status_complete(
    llhttp__internal_t *s, const unsigned char *p,
    const unsigned char *endp);

int llhttp__internal__c_update_type(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->type = 1;
    return 0;
}

int llhttp__internal__c_update_type_1(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    state->type = 2;
    return 0;
}

int llhttp__internal_init(llhttp__internal_t *state)
{
    memset(state, 0, sizeof(*state));
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_start;
    return 0;
}

static llparse_state_t llhttp__internal__run(
    llhttp__internal_t *state,
    const unsigned char *p,
    const unsigned char *endp)
{
    int match;
    switch ((llparse_state_t)(intptr_t)state->_current) {
    case s_n_llhttp__internal__n_closed:
    s_n_llhttp__internal__n_closed: {
        if (p == endp) {
            return s_n_llhttp__internal__n_closed;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_closed;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_closed;
        }
        default: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__after_message_complete:
    s_n_llhttp__internal__n_invoke_llhttp__after_message_complete: {
        switch (llhttp__after_message_complete(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_invoke_update_content_length;
        default:
            goto s_n_llhttp__internal__n_invoke_update_finish_1;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_pause_1:
    s_n_llhttp__internal__n_pause_1: {
        state->error = 0x16;
        state->reason = "Pause on CONNECT/Upgrade";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__after_message_complete;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_is_equal_upgrade:
    s_n_llhttp__internal__n_invoke_is_equal_upgrade: {
        switch (llhttp__internal__c_is_equal_upgrade(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_llhttp__after_message_complete;
        default:
            goto s_n_llhttp__internal__n_pause_1;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2:
    s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2: {
        switch (llhttp__on_message_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_is_equal_upgrade;
        case 21:
            goto s_n_llhttp__internal__n_pause_13;
        default:
            goto s_n_llhttp__internal__n_error_38;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_data_almost_done_1:
    s_n_llhttp__internal__n_chunk_data_almost_done_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_data_almost_done_1;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_7;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_data_almost_done:
    s_n_llhttp__internal__n_chunk_data_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_data_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_6;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_chunk_data_almost_done_1;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_7;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_consume_content_length:
    s_n_llhttp__internal__n_consume_content_length: {
        size_t avail;
        uint64_t need;

        avail = endp - p;
        need = state->content_length;
        if (avail >= need) {
            p += need;
            state->content_length = 0;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_body;
        }

        state->content_length -= avail;
        return s_n_llhttp__internal__n_consume_content_length;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_body:
    s_n_llhttp__internal__n_span_start_llhttp__on_body: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_body;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_body;
        goto s_n_llhttp__internal__n_consume_content_length;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_is_equal_content_length:
    s_n_llhttp__internal__n_invoke_is_equal_content_length: {
        switch (llhttp__internal__c_is_equal_content_length(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_span_start_llhttp__on_body;
        default:
            goto s_n_llhttp__internal__n_invoke_or_flags;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_size_almost_done:
    s_n_llhttp__internal__n_chunk_size_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_size_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_header;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_8;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_test_lenient_flags_9:
    s_n_llhttp__internal__n_invoke_test_lenient_flags_9: {
        switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        default:
            goto s_n_llhttp__internal__n_error_20;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete: {
        switch (llhttp__on_chunk_extension_name_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_9;
        case 21:
            goto s_n_llhttp__internal__n_pause_5;
        default:
            goto s_n_llhttp__internal__n_error_19;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_1:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_1: {
        switch (llhttp__on_chunk_extension_name_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        case 21:
            goto s_n_llhttp__internal__n_pause_6;
        default:
            goto s_n_llhttp__internal__n_error_21;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_2:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_2: {
        switch (llhttp__on_chunk_extension_name_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_chunk_extensions;
        case 21:
            goto s_n_llhttp__internal__n_pause_7;
        default:
            goto s_n_llhttp__internal__n_error_22;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_test_lenient_flags_10:
    s_n_llhttp__internal__n_invoke_test_lenient_flags_10: {
        switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        default:
            goto s_n_llhttp__internal__n_error_25;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete: {
        switch (llhttp__on_chunk_extension_value_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_10;
        case 21:
            goto s_n_llhttp__internal__n_pause_8;
        default:
            goto s_n_llhttp__internal__n_error_24;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_1:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_1: {
        switch (llhttp__on_chunk_extension_value_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        case 21:
            goto s_n_llhttp__internal__n_pause_9;
        default:
            goto s_n_llhttp__internal__n_error_26;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extension_quoted_value_done:
    s_n_llhttp__internal__n_chunk_extension_quoted_value_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extension_quoted_value_done;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_11;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        }
        case ';': {
            p++;
            goto s_n_llhttp__internal__n_chunk_extensions;
        }
        default: {
            goto s_n_llhttp__internal__n_error_29;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_2:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_2: {
        switch (llhttp__on_chunk_extension_value_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_chunk_extension_quoted_value_done;
        case 21:
            goto s_n_llhttp__internal__n_pause_10;
        default:
            goto s_n_llhttp__internal__n_error_27;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_30:
    s_n_llhttp__internal__n_error_30: {
        state->error = 0x2;
        state->reason = "Invalid quoted-pair in chunk extensions quoted value";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extension_quoted_value_quoted_pair:
    s_n_llhttp__internal__n_chunk_extension_quoted_value_quoted_pair: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extension_quoted_value_quoted_pair;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_quoted_value;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_31:
    s_n_llhttp__internal__n_error_31: {
        state->error = 0x2;
        state->reason = "Invalid character in chunk extensions quoted value";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extension_quoted_value:
    s_n_llhttp__internal__n_chunk_extension_quoted_value: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extension_quoted_value;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_quoted_value;
        }
        case 2: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_2;
        }
        case 3: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_quoted_value_quoted_pair;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_4;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_3:
    s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_3: {
        switch (llhttp__on_chunk_extension_value_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_chunk_extensions;
        case 21:
            goto s_n_llhttp__internal__n_pause_11;
        default:
            goto s_n_llhttp__internal__n_error_32;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_33:
    s_n_llhttp__internal__n_error_33: {
        state->error = 0x2;
        state->reason = "Invalid character in chunk extensions value";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extension_value:
    s_n_llhttp__internal__n_chunk_extension_value: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 3, 4, 3, 3, 3, 3, 3, 0, 0, 3, 3, 0, 3, 3, 0,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 5, 0, 0, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 0, 3, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extension_value;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_1;
        }
        case 3: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_value;
        }
        case 4: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_quoted_value;
        }
        case 5: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_5;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_6;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value:
    s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_chunk_extension_value;
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_3;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_34:
    s_n_llhttp__internal__n_error_34: {
        state->error = 0x2;
        state->reason = "Invalid character in chunk extensions name";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extension_name:
    s_n_llhttp__internal__n_chunk_extension_name: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 2, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 3, 0, 3, 3, 3, 3, 3, 0, 0, 3, 3, 0, 3, 3, 0,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 4, 0, 5, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 3, 0, 3, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extension_name;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_1;
        }
        case 3: {
            p++;
            goto s_n_llhttp__internal__n_chunk_extension_name;
        }
        case 4: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_2;
        }
        case 5: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_3;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_4;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_name:
    s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_name: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_name;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_chunk_extension_name;
        goto s_n_llhttp__internal__n_chunk_extension_name;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_extensions:
    s_n_llhttp__internal__n_chunk_extensions: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_extensions;
        }
        switch (*p) {
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_error_17;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_error_18;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_name;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_size_otherwise:
    s_n_llhttp__internal__n_chunk_size_otherwise: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_size_otherwise;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_4;
        }
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_5;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_chunk_size_almost_done;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_4;
        }
        case ';': {
            p++;
            goto s_n_llhttp__internal__n_chunk_extensions;
        }
        default: {
            goto s_n_llhttp__internal__n_error_35;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_size:
    s_n_llhttp__internal__n_chunk_size: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_size;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'A': {
            p++;
            match = 10;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'B': {
            p++;
            match = 11;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'C': {
            p++;
            match = 12;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'D': {
            p++;
            match = 13;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'E': {
            p++;
            match = 14;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'F': {
            p++;
            match = 15;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'a': {
            p++;
            match = 10;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'b': {
            p++;
            match = 11;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'c': {
            p++;
            match = 12;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'd': {
            p++;
            match = 13;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'e': {
            p++;
            match = 14;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'f': {
            p++;
            match = 15;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        default: {
            goto s_n_llhttp__internal__n_chunk_size_otherwise;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_chunk_size_digit:
    s_n_llhttp__internal__n_chunk_size_digit: {
        if (p == endp) {
            return s_n_llhttp__internal__n_chunk_size_digit;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'A': {
            p++;
            match = 10;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'B': {
            p++;
            match = 11;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'C': {
            p++;
            match = 12;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'D': {
            p++;
            match = 13;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'E': {
            p++;
            match = 14;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'F': {
            p++;
            match = 15;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'a': {
            p++;
            match = 10;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'b': {
            p++;
            match = 11;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'c': {
            p++;
            match = 12;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'd': {
            p++;
            match = 13;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'e': {
            p++;
            match = 14;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        case 'f': {
            p++;
            match = 15;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length;
        }
        default: {
            goto s_n_llhttp__internal__n_error_37;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_update_content_length_1:
    s_n_llhttp__internal__n_invoke_update_content_length_1: {
        switch (llhttp__internal__c_update_content_length(state, p, endp)) {
        default:
            goto s_n_llhttp__internal__n_chunk_size_digit;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_consume_content_length_1:
    s_n_llhttp__internal__n_consume_content_length_1: {
        size_t avail;
        uint64_t need;

        avail = endp - p;
        need = state->content_length;
        if (avail >= need) {
            p += need;
            state->content_length = 0;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_body_1;
        }

        state->content_length -= avail;
        return s_n_llhttp__internal__n_consume_content_length_1;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_body_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_body_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_body_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_body;
        goto s_n_llhttp__internal__n_consume_content_length_1;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_eof:
    s_n_llhttp__internal__n_eof: {
        if (p == endp) {
            return s_n_llhttp__internal__n_eof;
        }
        p++;
        goto s_n_llhttp__internal__n_eof;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_body_2:
    s_n_llhttp__internal__n_span_start_llhttp__on_body_2: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_body_2;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_body;
        goto s_n_llhttp__internal__n_eof;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete:
    s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete: {
        switch (llhttp__after_headers_complete(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_1;
        case 2:
            goto s_n_llhttp__internal__n_invoke_update_content_length_1;
        case 3:
            goto s_n_llhttp__internal__n_span_start_llhttp__on_body_1;
        case 4:
            goto s_n_llhttp__internal__n_invoke_update_finish_3;
        case 5:
            goto s_n_llhttp__internal__n_error_39;
        default:
            goto s_n_llhttp__internal__n_invoke_llhttp__on_message_complete;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_5:
    s_n_llhttp__internal__n_error_5: {
        state->error = 0xa;
        state->reason = "Invalid header field char";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_headers_almost_done:
    s_n_llhttp__internal__n_headers_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_headers_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_flags_1;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_12;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_colon_discard_ws:
    s_n_llhttp__internal__n_header_field_colon_discard_ws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_colon_discard_ws;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_field_colon_discard_ws;
        }
        default: {
            goto s_n_llhttp__internal__n_header_field_colon;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete: {
        switch (llhttp__on_header_value_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_header_field_start;
        case 21:
            goto s_n_llhttp__internal__n_pause_18;
        default:
            goto s_n_llhttp__internal__n_error_48;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_header_value:
    s_n_llhttp__internal__n_span_start_llhttp__on_header_value: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_header_value;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_header_value;
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_discard_lws:
    s_n_llhttp__internal__n_header_value_discard_lws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_discard_lws;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_15;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_15;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_load_header_state_1;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_discard_ws_almost_done:
    s_n_llhttp__internal__n_header_value_discard_ws_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_discard_ws_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_header_value_discard_lws;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_16;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_lws:
    s_n_llhttp__internal__n_header_value_lws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_lws;
        }
        switch (*p) {
        case 9: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_18;
        }
        case ' ': {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_18;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_load_header_state_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_almost_done:
    s_n_llhttp__internal__n_header_value_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_header_value_lws;
        }
        default: {
            goto s_n_llhttp__internal__n_error_53;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_test_lenient_flags_17:
    s_n_llhttp__internal__n_invoke_test_lenient_flags_17: {
        switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_header_value_almost_done;
        default:
            goto s_n_llhttp__internal__n_error_51;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_lenient:
    s_n_llhttp__internal__n_header_value_lenient: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_lenient;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_4;
        }
        case 13: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_5;
        }
        default: {
            p++;
            goto s_n_llhttp__internal__n_header_value_lenient;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_54:
    s_n_llhttp__internal__n_error_54: {
        state->error = 0xa;
        state->reason = "Invalid header value char";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_otherwise:
    s_n_llhttp__internal__n_header_value_otherwise: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_otherwise;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_1;
        }
        case 13: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_2;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_19;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection_token:
    s_n_llhttp__internal__n_header_value_connection_token: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection_token;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection_token;
        }
        case 2: {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_otherwise;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection_ws:
    s_n_llhttp__internal__n_header_value_connection_ws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection_ws;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_header_value_otherwise;
        }
        case 13: {
            goto s_n_llhttp__internal__n_header_value_otherwise;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection_ws;
        }
        case ',': {
            p++;
            goto s_n_llhttp__internal__n_invoke_load_header_state_6;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection_1:
    s_n_llhttp__internal__n_header_value_connection_1: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection_1;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob2, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_invoke_update_header_state_3;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_value_connection_1;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_header_value_connection_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection_2:
    s_n_llhttp__internal__n_header_value_connection_2: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection_2;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob3, 9);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_invoke_update_header_state_6;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_value_connection_2;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_header_value_connection_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection_3:
    s_n_llhttp__internal__n_header_value_connection_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection_3;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob4, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_invoke_update_header_state_7;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_value_connection_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_header_value_connection_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_connection:
    s_n_llhttp__internal__n_header_value_connection: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_connection;
        }
        switch (((*p) >= 'A' && (*p) <= 'Z' ? (*p | 0x20) : (*p))) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection;
        }
        case 'c': {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection_1;
        }
        case 'k': {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection_2;
        }
        case 'u': {
            p++;
            goto s_n_llhttp__internal__n_header_value_connection_3;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_connection_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_56:
    s_n_llhttp__internal__n_error_56: {
        state->error = 0xb;
        state->reason = "Content-Length overflow";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_57:
    s_n_llhttp__internal__n_error_57: {
        state->error = 0xb;
        state->reason = "Invalid character in Content-Length";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_content_length_ws:
    s_n_llhttp__internal__n_header_value_content_length_ws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_content_length_ws;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_invoke_or_flags_17;
        }
        case 13: {
            goto s_n_llhttp__internal__n_invoke_or_flags_17;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_content_length_ws;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_7;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_content_length:
    s_n_llhttp__internal__n_header_value_content_length: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_content_length;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_content_length_1;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_content_length_ws;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_59:
    s_n_llhttp__internal__n_error_59: {
        state->error = 0xf;
        state->reason = "Invalid `Transfer-Encoding` header value";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_58:
    s_n_llhttp__internal__n_error_58: {
        state->error = 0xf;
        state->reason = "Invalid `Transfer-Encoding` header value";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_te_token_ows:
    s_n_llhttp__internal__n_header_value_te_token_ows: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_te_token_ows;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_token_ows;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_token_ows;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_te_chunked;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value:
    s_n_llhttp__internal__n_header_value: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value;
        }
#ifdef __SSE4_2__
        if (endp - p >= 16) {
            __m128i ranges;
            __m128i input;
            int match_len;

            /* Load input */
            input = _mm_loadu_si128((__m128i const *)p);
            ranges = _mm_loadu_si128((__m128i const *)llparse_blob6);

            /* Find first character that does not match `ranges` */
            match_len = _mm_cmpestri(ranges, 6,
                                     input, 16,
                                     _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES |
                                         _SIDD_NEGATIVE_POLARITY);

            if (match_len != 0) {
                p += match_len;
                goto s_n_llhttp__internal__n_header_value;
            }
            goto s_n_llhttp__internal__n_header_value_otherwise;
        }
#endif /* __SSE4_2__ */
#ifdef __ARM_NEON__
        while (endp - p >= 16) {
            uint8x16_t input;
            uint8x16_t single;
            uint8x16_t mask;
            uint8x8_t narrow;
            uint64_t match_mask;
            int match_len;

            /* Load input */
            input = vld1q_u8(p);
            /* Find first character that does not match `ranges` */
            single = vceqq_u8(input, vdupq_n_u8(0x9));
            mask = single;
            single = vandq_u16(
                vcgeq_u8(input, vdupq_n_u8(' ')),
                vcleq_u8(input, vdupq_n_u8('~')));
            mask = vorrq_u16(mask, single);
            single = vandq_u16(
                vcgeq_u8(input, vdupq_n_u8(0x80)),
                vcleq_u8(input, vdupq_n_u8(0xff)));
            mask = vorrq_u16(mask, single);
            narrow = vshrn_n_u16(mask, 4);
            match_mask = ~vget_lane_u64(vreinterpret_u64_u8(narrow), 0);
            match_len = __builtin_ctzll(match_mask) >> 2;
            if (match_len != 16) {
                p += match_len;
                goto s_n_llhttp__internal__n_header_value_otherwise;
            }
            p += 16;
        }
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value;
        }
#endif /* __ARM_NEON__ */
#ifdef __wasm_simd128__
        while (endp - p >= 16) {
            v128_t input;
            v128_t mask;
            v128_t single;
            int match_len;

            /* Load input */
            input = wasm_v128_load(p);
            /* Find first character that does not match `ranges` */
            single = wasm_i8x16_eq(input, wasm_u8x16_const_splat(0x9));
            mask = single;
            single = wasm_v128_and(
                wasm_i8x16_ge(input, wasm_u8x16_const_splat(' ')),
                wasm_i8x16_le(input, wasm_u8x16_const_splat('~')));
            mask = wasm_v128_or(mask, single);
            single = wasm_v128_and(
                wasm_i8x16_ge(input, wasm_u8x16_const_splat(0x80)),
                wasm_i8x16_le(input, wasm_u8x16_const_splat(0xff)));
            mask = wasm_v128_or(mask, single);
            match_len = __builtin_ctz(
                ~wasm_i8x16_bitmask(mask));
            if (match_len != 16) {
                p += match_len;
                goto s_n_llhttp__internal__n_header_value_otherwise;
            }
            p += 16;
        }
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value;
        }
#endif /* __wasm_simd128__ */
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_header_value;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_otherwise;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_te_token:
    s_n_llhttp__internal__n_header_value_te_token: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_te_token;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_token;
        }
        case 2: {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_token_ows;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_9;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_te_chunked_last:
    s_n_llhttp__internal__n_header_value_te_chunked_last: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_te_chunked_last;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_8;
        }
        case 13: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_8;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_chunked_last;
        }
        case ',': {
            goto s_n_llhttp__internal__n_invoke_load_type_1;
        }
        default: {
            goto s_n_llhttp__internal__n_header_value_te_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_te_chunked:
    s_n_llhttp__internal__n_header_value_te_chunked: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_te_chunked;
        }
        match_seq = llparse__match_sequence_to_lower_unsafe(state, p, endp, llparse_blob5, 7);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_header_value_te_chunked_last;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_value_te_chunked;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_header_value_te_token;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_header_value;
        goto s_n_llhttp__internal__n_invoke_load_header_state_3;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_value_discard_ws:
    s_n_llhttp__internal__n_header_value_discard_ws: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_value_discard_ws;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_header_value_discard_ws;
        }
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_14;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_header_value_discard_ws_almost_done;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_header_value_discard_ws;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_header_state:
    s_n_llhttp__internal__n_invoke_load_header_state: {
        switch (llhttp__internal__c_load_header_state(state, p, endp)) {
        case 2:
            goto s_n_llhttp__internal__n_invoke_test_flags_4;
        case 3:
            goto s_n_llhttp__internal__n_invoke_test_flags_5;
        default:
            goto s_n_llhttp__internal__n_header_value_discard_ws;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete: {
        switch (llhttp__on_header_field_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_load_header_state;
        case 21:
            goto s_n_llhttp__internal__n_pause_19;
        default:
            goto s_n_llhttp__internal__n_error_45;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_general_otherwise:
    s_n_llhttp__internal__n_header_field_general_otherwise: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_general_otherwise;
        }
        switch (*p) {
        case ':': {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_field_2;
        }
        default: {
            goto s_n_llhttp__internal__n_error_62;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_general:
    s_n_llhttp__internal__n_header_field_general: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 1, 0, 1, 1, 1, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0,
            0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_general;
        }
#ifdef __SSE4_2__
        if (endp - p >= 16) {
            __m128i ranges;
            __m128i input;
            int match_len;

            /* Load input */
            input = _mm_loadu_si128((__m128i const *)p);
            ranges = _mm_loadu_si128((__m128i const *)llparse_blob7);

            /* Find first character that does not match `ranges` */
            match_len = _mm_cmpestri(ranges, 16,
                                     input, 16,
                                     _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES |
                                         _SIDD_NEGATIVE_POLARITY);

            if (match_len != 0) {
                p += match_len;
                goto s_n_llhttp__internal__n_header_field_general;
            }
            ranges = _mm_loadu_si128((__m128i const *)llparse_blob8);

            /* Find first character that does not match `ranges` */
            match_len = _mm_cmpestri(ranges, 2,
                                     input, 16,
                                     _SIDD_UBYTE_OPS | _SIDD_CMP_RANGES |
                                         _SIDD_NEGATIVE_POLARITY);

            if (match_len != 0) {
                p += match_len;
                goto s_n_llhttp__internal__n_header_field_general;
            }
            goto s_n_llhttp__internal__n_header_field_general_otherwise;
        }
#endif /* __SSE4_2__ */
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_header_field_general;
        }
        default: {
            goto s_n_llhttp__internal__n_header_field_general_otherwise;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_colon:
    s_n_llhttp__internal__n_header_field_colon: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_colon;
        }
        switch (*p) {
        case ' ': {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_13;
        }
        case ':': {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_header_field_1;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_10;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_3:
    s_n_llhttp__internal__n_header_field_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_3;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob1, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_header_state;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_4:
    s_n_llhttp__internal__n_header_field_4: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_4;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob9, 10);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_header_state;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_4;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_2:
    s_n_llhttp__internal__n_header_field_2: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_2;
        }
        switch (((*p) >= 'A' && (*p) <= 'Z' ? (*p | 0x20) : (*p))) {
        case 'n': {
            p++;
            goto s_n_llhttp__internal__n_header_field_3;
        }
        case 't': {
            p++;
            goto s_n_llhttp__internal__n_header_field_4;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_1:
    s_n_llhttp__internal__n_header_field_1: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_1;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob0, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_header_field_2;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_1;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_5:
    s_n_llhttp__internal__n_header_field_5: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_5;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob10, 15);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_header_state;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_5;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_6:
    s_n_llhttp__internal__n_header_field_6: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_6;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob11, 16);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_header_state;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_6;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_7:
    s_n_llhttp__internal__n_header_field_7: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_7;
        }
        match_seq = llparse__match_sequence_to_lower(state, p, endp, llparse_blob12, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_header_state;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_header_field_7;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field:
    s_n_llhttp__internal__n_header_field: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field;
        }
        switch (((*p) >= 'A' && (*p) <= 'Z' ? (*p | 0x20) : (*p))) {
        case 'c': {
            p++;
            goto s_n_llhttp__internal__n_header_field_1;
        }
        case 'p': {
            p++;
            goto s_n_llhttp__internal__n_header_field_5;
        }
        case 't': {
            p++;
            goto s_n_llhttp__internal__n_header_field_6;
        }
        case 'u': {
            p++;
            goto s_n_llhttp__internal__n_header_field_7;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_header_state_11;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_header_field:
    s_n_llhttp__internal__n_span_start_llhttp__on_header_field: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_header_field;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_header_field;
        goto s_n_llhttp__internal__n_header_field;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_header_field_start:
    s_n_llhttp__internal__n_header_field_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_header_field_start;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_1;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_headers_almost_done;
        }
        case ':': {
            goto s_n_llhttp__internal__n_error_44;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_header_field;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_headers_start:
    s_n_llhttp__internal__n_headers_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_headers_start;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags;
        }
        default: {
            goto s_n_llhttp__internal__n_header_field_start;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_to_http_09:
    s_n_llhttp__internal__n_url_to_http_09: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_to_http_09;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_http_major;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_skip_to_http09:
    s_n_llhttp__internal__n_url_skip_to_http09: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_skip_to_http09;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            p++;
            goto s_n_llhttp__internal__n_url_to_http_09;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_skip_lf_to_http09_1:
    s_n_llhttp__internal__n_url_skip_lf_to_http09_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_skip_lf_to_http09_1;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_url_to_http_09;
        }
        default: {
            goto s_n_llhttp__internal__n_error_63;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_skip_lf_to_http09:
    s_n_llhttp__internal__n_url_skip_lf_to_http09: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_skip_lf_to_http09;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_url_skip_lf_to_http09_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_63;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_pri_upgrade:
    s_n_llhttp__internal__n_req_pri_upgrade: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_pri_upgrade;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob14, 10);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_error_72;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_pri_upgrade;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_73;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_complete_crlf:
    s_n_llhttp__internal__n_req_http_complete_crlf: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_complete_crlf;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_headers_start;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_26;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_complete:
    s_n_llhttp__internal__n_req_http_complete: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_complete;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_25;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_req_http_complete_crlf;
        }
        default: {
            goto s_n_llhttp__internal__n_error_71;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_method_1:
    s_n_llhttp__internal__n_invoke_load_method_1: {
        switch (llhttp__internal__c_load_method(state, p, endp)) {
        case 34:
            goto s_n_llhttp__internal__n_req_pri_upgrade;
        default:
            goto s_n_llhttp__internal__n_req_http_complete;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_version_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_version_complete: {
        switch (llhttp__on_version_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_load_method_1;
        case 21:
            goto s_n_llhttp__internal__n_pause_21;
        default:
            goto s_n_llhttp__internal__n_error_68;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_67:
    s_n_llhttp__internal__n_error_67: {
        state->error = 0x9;
        state->reason = "Invalid HTTP version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_74:
    s_n_llhttp__internal__n_error_74: {
        state->error = 0x9;
        state->reason = "Invalid minor version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_minor:
    s_n_llhttp__internal__n_req_http_minor: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_minor;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_store_http_minor;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_2;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_75:
    s_n_llhttp__internal__n_error_75: {
        state->error = 0x9;
        state->reason = "Expected dot";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_dot:
    s_n_llhttp__internal__n_req_http_dot: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_dot;
        }
        switch (*p) {
        case '.': {
            p++;
            goto s_n_llhttp__internal__n_req_http_minor;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_76:
    s_n_llhttp__internal__n_error_76: {
        state->error = 0x9;
        state->reason = "Invalid major version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_major:
    s_n_llhttp__internal__n_req_http_major: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_major;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_store_http_major;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_4;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_version:
    s_n_llhttp__internal__n_span_start_llhttp__on_version: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_version;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_version;
        goto s_n_llhttp__internal__n_req_http_major;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_after_protocol:
    s_n_llhttp__internal__n_req_after_protocol: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_after_protocol;
        }
        switch (*p) {
        case '/': {
            p++;
            goto s_n_llhttp__internal__n_span_start_llhttp__on_version;
        }
        default: {
            goto s_n_llhttp__internal__n_error_77;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_method:
    s_n_llhttp__internal__n_invoke_load_method: {
        switch (llhttp__internal__c_load_method(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 1:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 2:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 3:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 4:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 5:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 6:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 7:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 8:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 9:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 10:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 11:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 12:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 13:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 14:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 15:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 16:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 17:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 18:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 19:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 20:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 21:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 22:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 23:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 24:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 25:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 26:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 27:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 28:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 29:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 30:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 31:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 32:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 33:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 34:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 46:
            goto s_n_llhttp__internal__n_req_after_protocol;
        default:
            goto s_n_llhttp__internal__n_error_66;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete: {
        switch (llhttp__on_protocol_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_load_method;
        case 21:
            goto s_n_llhttp__internal__n_pause_22;
        default:
            goto s_n_llhttp__internal__n_error_65;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_82:
    s_n_llhttp__internal__n_error_82: {
        state->error = 0x8;
        state->reason = "Expected HTTP/, RTSP/ or ICE/";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_after_http_start_1:
    s_n_llhttp__internal__n_req_after_http_start_1: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_after_http_start_1;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob13, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_after_http_start_1;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_method_2:
    s_n_llhttp__internal__n_invoke_load_method_2: {
        switch (llhttp__internal__c_load_method(state, p, endp)) {
        case 33:
            goto s_n_llhttp__internal__n_req_after_protocol;
        default:
            goto s_n_llhttp__internal__n_error_79;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_1:
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_1: {
        switch (llhttp__on_protocol_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_load_method_2;
        case 21:
            goto s_n_llhttp__internal__n_pause_23;
        default:
            goto s_n_llhttp__internal__n_error_78;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_after_http_start_2:
    s_n_llhttp__internal__n_req_after_http_start_2: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_after_http_start_2;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob15, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_after_http_start_2;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_method_3:
    s_n_llhttp__internal__n_invoke_load_method_3: {
        switch (llhttp__internal__c_load_method(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 3:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 6:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 35:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 36:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 37:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 38:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 39:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 40:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 41:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 42:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 43:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 44:
            goto s_n_llhttp__internal__n_req_after_protocol;
        case 45:
            goto s_n_llhttp__internal__n_req_after_protocol;
        default:
            goto s_n_llhttp__internal__n_error_81;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_2:
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_2: {
        switch (llhttp__on_protocol_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_invoke_load_method_3;
        case 21:
            goto s_n_llhttp__internal__n_pause_24;
        default:
            goto s_n_llhttp__internal__n_error_80;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_after_http_start_3:
    s_n_llhttp__internal__n_req_after_http_start_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_after_http_start_3;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob16, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_2;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_after_http_start_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_after_http_start:
    s_n_llhttp__internal__n_req_after_http_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_after_http_start;
        }
        switch (*p) {
        case 'H': {
            p++;
            goto s_n_llhttp__internal__n_req_after_http_start_1;
        }
        case 'I': {
            p++;
            goto s_n_llhttp__internal__n_req_after_http_start_2;
        }
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_req_after_http_start_3;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_3;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_protocol:
    s_n_llhttp__internal__n_span_start_llhttp__on_protocol: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_protocol;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_protocol;
        goto s_n_llhttp__internal__n_req_after_http_start;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_http_start:
    s_n_llhttp__internal__n_req_http_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_http_start;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_req_http_start;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_protocol;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_to_http:
    s_n_llhttp__internal__n_url_to_http: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_to_http;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_llhttp__on_url_complete_1;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_skip_to_http:
    s_n_llhttp__internal__n_url_skip_to_http: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_skip_to_http;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            p++;
            goto s_n_llhttp__internal__n_url_to_http;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_fragment:
    s_n_llhttp__internal__n_url_fragment: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 1, 3, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            4, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_fragment;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_6;
        }
        case 3: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_7;
        }
        case 4: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_8;
        }
        case 5: {
            p++;
            goto s_n_llhttp__internal__n_url_fragment;
        }
        default: {
            goto s_n_llhttp__internal__n_error_83;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_end_stub_query_3:
    s_n_llhttp__internal__n_span_end_stub_query_3: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_end_stub_query_3;
        }
        p++;
        goto s_n_llhttp__internal__n_url_fragment;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_query:
    s_n_llhttp__internal__n_url_query: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 1, 3, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            4, 5, 5, 6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_query;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_9;
        }
        case 3: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_10;
        }
        case 4: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_11;
        }
        case 5: {
            p++;
            goto s_n_llhttp__internal__n_url_query;
        }
        case 6: {
            goto s_n_llhttp__internal__n_span_end_stub_query_3;
        }
        default: {
            goto s_n_llhttp__internal__n_error_84;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_query_or_fragment:
    s_n_llhttp__internal__n_url_query_or_fragment: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_query_or_fragment;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 10: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_3;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 13: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_4;
        }
        case ' ': {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_5;
        }
        case '#': {
            p++;
            goto s_n_llhttp__internal__n_url_fragment;
        }
        case '?': {
            p++;
            goto s_n_llhttp__internal__n_url_query;
        }
        default: {
            goto s_n_llhttp__internal__n_error_85;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_path:
    s_n_llhttp__internal__n_url_path: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 2, 2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_path;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            p++;
            goto s_n_llhttp__internal__n_url_path;
        }
        default: {
            goto s_n_llhttp__internal__n_url_query_or_fragment;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_stub_path_2:
    s_n_llhttp__internal__n_span_start_stub_path_2: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_stub_path_2;
        }
        p++;
        goto s_n_llhttp__internal__n_url_path;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_stub_path:
    s_n_llhttp__internal__n_span_start_stub_path: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_stub_path;
        }
        p++;
        goto s_n_llhttp__internal__n_url_path;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_stub_path_1:
    s_n_llhttp__internal__n_span_start_stub_path_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_stub_path_1;
        }
        p++;
        goto s_n_llhttp__internal__n_url_path;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_server_with_at:
    s_n_llhttp__internal__n_url_server_with_at: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 1, 3, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            4, 5, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 5, 0, 7,
            8, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 5, 0, 5,
            0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 5, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_server_with_at;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_12;
        }
        case 3: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_13;
        }
        case 4: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_14;
        }
        case 5: {
            p++;
            goto s_n_llhttp__internal__n_url_server;
        }
        case 6: {
            goto s_n_llhttp__internal__n_span_start_stub_path_1;
        }
        case 7: {
            p++;
            goto s_n_llhttp__internal__n_url_query;
        }
        case 8: {
            p++;
            goto s_n_llhttp__internal__n_error_86;
        }
        default: {
            goto s_n_llhttp__internal__n_error_87;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_server:
    s_n_llhttp__internal__n_url_server: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 0, 1, 3, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            4, 5, 0, 0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 6,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 5, 0, 7,
            8, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 5, 0, 5,
            0, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
            5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 0, 0, 0, 5, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_server;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url;
        }
        case 3: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_1;
        }
        case 4: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_url_2;
        }
        case 5: {
            p++;
            goto s_n_llhttp__internal__n_url_server;
        }
        case 6: {
            goto s_n_llhttp__internal__n_span_start_stub_path;
        }
        case 7: {
            p++;
            goto s_n_llhttp__internal__n_url_query;
        }
        case 8: {
            p++;
            goto s_n_llhttp__internal__n_url_server_with_at;
        }
        default: {
            goto s_n_llhttp__internal__n_error_88;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_schema_delim_1:
    s_n_llhttp__internal__n_url_schema_delim_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_schema_delim_1;
        }
        switch (*p) {
        case '/': {
            p++;
            goto s_n_llhttp__internal__n_url_server;
        }
        default: {
            goto s_n_llhttp__internal__n_error_89;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_schema_delim:
    s_n_llhttp__internal__n_url_schema_delim: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_schema_delim;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case '/': {
            p++;
            goto s_n_llhttp__internal__n_url_schema_delim_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_89;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_end_stub_schema:
    s_n_llhttp__internal__n_span_end_stub_schema: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_end_stub_schema;
        }
        p++;
        goto s_n_llhttp__internal__n_url_schema_delim;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_schema:
    s_n_llhttp__internal__n_url_schema: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_schema;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_end_stub_schema;
        }
        case 3: {
            p++;
            goto s_n_llhttp__internal__n_url_schema;
        }
        default: {
            goto s_n_llhttp__internal__n_error_90;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_start:
    s_n_llhttp__internal__n_url_start: {
        static uint8_t lookup_table[] = {
            0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
            0, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        if (p == endp) {
            return s_n_llhttp__internal__n_url_start;
        }
        switch (lookup_table[(uint8_t)*p]) {
        case 1: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 2: {
            goto s_n_llhttp__internal__n_span_start_stub_path_2;
        }
        case 3: {
            goto s_n_llhttp__internal__n_url_schema;
        }
        default: {
            goto s_n_llhttp__internal__n_error_91;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_url_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_url_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_url_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_url;
        goto s_n_llhttp__internal__n_url_start;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_entry_normal:
    s_n_llhttp__internal__n_url_entry_normal: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_entry_normal;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_url_1;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_url:
    s_n_llhttp__internal__n_span_start_llhttp__on_url: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_url;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_url;
        goto s_n_llhttp__internal__n_url_server;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_url_entry_connect:
    s_n_llhttp__internal__n_url_entry_connect: {
        if (p == endp) {
            return s_n_llhttp__internal__n_url_entry_connect;
        }
        switch (*p) {
        case 9: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        case 12: {
            p++;
            goto s_n_llhttp__internal__n_error_2;
        }
        default: {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_url;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_spaces_before_url:
    s_n_llhttp__internal__n_req_spaces_before_url: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_spaces_before_url;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_req_spaces_before_url;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_is_equal_method;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_first_space_before_url:
    s_n_llhttp__internal__n_req_first_space_before_url: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_first_space_before_url;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_req_spaces_before_url;
        }
        default: {
            goto s_n_llhttp__internal__n_error_92;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_method_complete_1:
    s_n_llhttp__internal__n_invoke_llhttp__on_method_complete_1: {
        switch (llhttp__on_method_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_req_first_space_before_url;
        case 21:
            goto s_n_llhttp__internal__n_pause_29;
        default:
            goto s_n_llhttp__internal__n_error_111;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_2:
    s_n_llhttp__internal__n_after_start_req_2: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_2;
        }
        switch (*p) {
        case 'L': {
            p++;
            match = 19;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_3:
    s_n_llhttp__internal__n_after_start_req_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_3;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob17, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 36;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_1:
    s_n_llhttp__internal__n_after_start_req_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_1;
        }
        switch (*p) {
        case 'C': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_2;
        }
        case 'N': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_3;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_4:
    s_n_llhttp__internal__n_after_start_req_4: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_4;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob18, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 16;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_4;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_6:
    s_n_llhttp__internal__n_after_start_req_6: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_6;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob19, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 22;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_6;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_8:
    s_n_llhttp__internal__n_after_start_req_8: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_8;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob20, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_8;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_9:
    s_n_llhttp__internal__n_after_start_req_9: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_9;
        }
        switch (*p) {
        case 'Y': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_7:
    s_n_llhttp__internal__n_after_start_req_7: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_7;
        }
        switch (*p) {
        case 'N': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_8;
        }
        case 'P': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_9;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_5:
    s_n_llhttp__internal__n_after_start_req_5: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_5;
        }
        switch (*p) {
        case 'H': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_6;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_7;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_12:
    s_n_llhttp__internal__n_after_start_req_12: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_12;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob21, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_12;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_13:
    s_n_llhttp__internal__n_after_start_req_13: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_13;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob22, 5);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 35;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_13;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_11:
    s_n_llhttp__internal__n_after_start_req_11: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_11;
        }
        switch (*p) {
        case 'L': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_12;
        }
        case 'S': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_13;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_10:
    s_n_llhttp__internal__n_after_start_req_10: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_10;
        }
        switch (*p) {
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_11;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_14:
    s_n_llhttp__internal__n_after_start_req_14: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_14;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob23, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 45;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_14;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_17:
    s_n_llhttp__internal__n_after_start_req_17: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_17;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob25, 9);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 41;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_17;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_16:
    s_n_llhttp__internal__n_after_start_req_16: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_16;
        }
        switch (*p) {
        case '_': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_17;
        }
        default: {
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_15:
    s_n_llhttp__internal__n_after_start_req_15: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_15;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob24, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_16;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_15;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_18:
    s_n_llhttp__internal__n_after_start_req_18: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_18;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob26, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_18;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_20:
    s_n_llhttp__internal__n_after_start_req_20: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_20;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob27, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 31;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_20;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_21:
    s_n_llhttp__internal__n_after_start_req_21: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_21;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob28, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_21;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_19:
    s_n_llhttp__internal__n_after_start_req_19: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_19;
        }
        switch (*p) {
        case 'I': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_20;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_21;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_23:
    s_n_llhttp__internal__n_after_start_req_23: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_23;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob29, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 24;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_23;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_24:
    s_n_llhttp__internal__n_after_start_req_24: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_24;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob30, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 23;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_24;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_26:
    s_n_llhttp__internal__n_after_start_req_26: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_26;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob31, 7);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 21;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_26;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_28:
    s_n_llhttp__internal__n_after_start_req_28: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_28;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob32, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 30;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_28;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_29:
    s_n_llhttp__internal__n_after_start_req_29: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_29;
        }
        switch (*p) {
        case 'L': {
            p++;
            match = 10;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_27:
    s_n_llhttp__internal__n_after_start_req_27: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_27;
        }
        switch (*p) {
        case 'A': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_28;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_29;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_25:
    s_n_llhttp__internal__n_after_start_req_25: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_25;
        }
        switch (*p) {
        case 'A': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_26;
        }
        case 'C': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_27;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_30:
    s_n_llhttp__internal__n_after_start_req_30: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_30;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob33, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 11;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_30;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_22:
    s_n_llhttp__internal__n_after_start_req_22: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_22;
        }
        switch (*p) {
        case '-': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_23;
        }
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_24;
        }
        case 'K': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_25;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_30;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_31:
    s_n_llhttp__internal__n_after_start_req_31: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_31;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob34, 5);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 25;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_31;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_32:
    s_n_llhttp__internal__n_after_start_req_32: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_32;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob35, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_32;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_35:
    s_n_llhttp__internal__n_after_start_req_35: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_35;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob36, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 28;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_35;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_36:
    s_n_llhttp__internal__n_after_start_req_36: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_36;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob37, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 39;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_36;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_34:
    s_n_llhttp__internal__n_after_start_req_34: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_34;
        }
        switch (*p) {
        case 'T': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_35;
        }
        case 'U': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_36;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_37:
    s_n_llhttp__internal__n_after_start_req_37: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_37;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob38, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 38;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_37;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_38:
    s_n_llhttp__internal__n_after_start_req_38: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_38;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob39, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_38;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_42:
    s_n_llhttp__internal__n_after_start_req_42: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_42;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob40, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 12;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_42;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_43:
    s_n_llhttp__internal__n_after_start_req_43: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_43;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob41, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 13;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_43;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_41:
    s_n_llhttp__internal__n_after_start_req_41: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_41;
        }
        switch (*p) {
        case 'F': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_42;
        }
        case 'P': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_43;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_40:
    s_n_llhttp__internal__n_after_start_req_40: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_40;
        }
        switch (*p) {
        case 'P': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_41;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_39:
    s_n_llhttp__internal__n_after_start_req_39: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_39;
        }
        switch (*p) {
        case 'I': {
            p++;
            match = 34;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_40;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_45:
    s_n_llhttp__internal__n_after_start_req_45: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_45;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob42, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 29;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_45;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_44:
    s_n_llhttp__internal__n_after_start_req_44: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_44;
        }
        switch (*p) {
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_45;
        }
        case 'T': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_33:
    s_n_llhttp__internal__n_after_start_req_33: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_33;
        }
        switch (*p) {
        case 'A': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_34;
        }
        case 'L': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_37;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_38;
        }
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_39;
        }
        case 'U': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_44;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_46:
    s_n_llhttp__internal__n_after_start_req_46: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_46;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob43, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 46;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_46;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_49:
    s_n_llhttp__internal__n_after_start_req_49: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_49;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob44, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 17;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_49;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_50:
    s_n_llhttp__internal__n_after_start_req_50: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_50;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob45, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 44;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_50;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_51:
    s_n_llhttp__internal__n_after_start_req_51: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_51;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob46, 5);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 43;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_51;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_52:
    s_n_llhttp__internal__n_after_start_req_52: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_52;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob47, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 20;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_52;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_48:
    s_n_llhttp__internal__n_after_start_req_48: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_48;
        }
        switch (*p) {
        case 'B': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_49;
        }
        case 'C': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_50;
        }
        case 'D': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_51;
        }
        case 'P': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_52;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_47:
    s_n_llhttp__internal__n_after_start_req_47: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_47;
        }
        switch (*p) {
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_48;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_55:
    s_n_llhttp__internal__n_after_start_req_55: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_55;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob48, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 14;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_55;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_57:
    s_n_llhttp__internal__n_after_start_req_57: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_57;
        }
        switch (*p) {
        case 'P': {
            p++;
            match = 37;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_58:
    s_n_llhttp__internal__n_after_start_req_58: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_58;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob49, 9);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 42;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_58;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_56:
    s_n_llhttp__internal__n_after_start_req_56: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_56;
        }
        switch (*p) {
        case 'U': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_57;
        }
        case '_': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_58;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_54:
    s_n_llhttp__internal__n_after_start_req_54: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_54;
        }
        switch (*p) {
        case 'A': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_55;
        }
        case 'T': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_56;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_59:
    s_n_llhttp__internal__n_after_start_req_59: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_59;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob50, 4);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 33;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_59;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_60:
    s_n_llhttp__internal__n_after_start_req_60: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_60;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob51, 7);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 26;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_60;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_53:
    s_n_llhttp__internal__n_after_start_req_53: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_53;
        }
        switch (*p) {
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_54;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_59;
        }
        case 'U': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_60;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_62:
    s_n_llhttp__internal__n_after_start_req_62: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_62;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob52, 6);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 40;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_62;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_63:
    s_n_llhttp__internal__n_after_start_req_63: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_63;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob53, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_63;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_61:
    s_n_llhttp__internal__n_after_start_req_61: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_61;
        }
        switch (*p) {
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_62;
        }
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_63;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_66:
    s_n_llhttp__internal__n_after_start_req_66: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_66;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob54, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 18;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_66;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_68:
    s_n_llhttp__internal__n_after_start_req_68: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_68;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob55, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 32;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_68;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_69:
    s_n_llhttp__internal__n_after_start_req_69: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_69;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob56, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 15;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_69;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_67:
    s_n_llhttp__internal__n_after_start_req_67: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_67;
        }
        switch (*p) {
        case 'I': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_68;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_69;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_70:
    s_n_llhttp__internal__n_after_start_req_70: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_70;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob57, 8);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 27;
            goto s_n_llhttp__internal__n_invoke_store_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_after_start_req_70;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_65:
    s_n_llhttp__internal__n_after_start_req_65: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_65;
        }
        switch (*p) {
        case 'B': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_66;
        }
        case 'L': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_67;
        }
        case 'S': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_70;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req_64:
    s_n_llhttp__internal__n_after_start_req_64: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req_64;
        }
        switch (*p) {
        case 'N': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_65;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_after_start_req:
    s_n_llhttp__internal__n_after_start_req: {
        if (p == endp) {
            return s_n_llhttp__internal__n_after_start_req;
        }
        switch (*p) {
        case 'A': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_1;
        }
        case 'B': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_4;
        }
        case 'C': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_5;
        }
        case 'D': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_10;
        }
        case 'F': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_14;
        }
        case 'G': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_15;
        }
        case 'H': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_18;
        }
        case 'L': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_19;
        }
        case 'M': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_22;
        }
        case 'N': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_31;
        }
        case 'O': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_32;
        }
        case 'P': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_33;
        }
        case 'Q': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_46;
        }
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_47;
        }
        case 'S': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_53;
        }
        case 'T': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_61;
        }
        case 'U': {
            p++;
            goto s_n_llhttp__internal__n_after_start_req_64;
        }
        default: {
            goto s_n_llhttp__internal__n_error_112;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_method_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_method_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_method_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_method;
        goto s_n_llhttp__internal__n_after_start_req;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_line_almost_done:
    s_n_llhttp__internal__n_res_line_almost_done: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_line_almost_done;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_llhttp__on_status_complete;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_invoke_llhttp__on_status_complete;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_29;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_test_lenient_flags_30:
    s_n_llhttp__internal__n_invoke_test_lenient_flags_30: {
        switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_invoke_llhttp__on_status_complete;
        default:
            goto s_n_llhttp__internal__n_error_98;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_status:
    s_n_llhttp__internal__n_res_status: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_status;
        }
        switch (*p) {
        case 10: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_status;
        }
        case 13: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_status_1;
        }
        default: {
            p++;
            goto s_n_llhttp__internal__n_res_status;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_status:
    s_n_llhttp__internal__n_span_start_llhttp__on_status: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_status;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_status;
        goto s_n_llhttp__internal__n_res_status;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_status_code_otherwise:
    s_n_llhttp__internal__n_res_status_code_otherwise: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_status_code_otherwise;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_invoke_test_lenient_flags_28;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_res_line_almost_done;
        }
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_span_start_llhttp__on_status;
        }
        default: {
            goto s_n_llhttp__internal__n_error_99;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_status_code_digit_3:
    s_n_llhttp__internal__n_res_status_code_digit_3: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_status_code_digit_3;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_2;
        }
        default: {
            goto s_n_llhttp__internal__n_error_101;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_status_code_digit_2:
    s_n_llhttp__internal__n_res_status_code_digit_2: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_status_code_digit_2;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_103;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_status_code_digit_1:
    s_n_llhttp__internal__n_res_status_code_digit_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_status_code_digit_1;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_mul_add_status_code;
        }
        default: {
            goto s_n_llhttp__internal__n_error_105;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_version:
    s_n_llhttp__internal__n_res_after_version: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_version;
        }
        switch (*p) {
        case ' ': {
            p++;
            goto s_n_llhttp__internal__n_invoke_update_status_code;
        }
        default: {
            goto s_n_llhttp__internal__n_error_106;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_version_complete_1:
    s_n_llhttp__internal__n_invoke_llhttp__on_version_complete_1: {
        switch (llhttp__on_version_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_res_after_version;
        case 21:
            goto s_n_llhttp__internal__n_pause_28;
        default:
            goto s_n_llhttp__internal__n_error_94;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_93:
    s_n_llhttp__internal__n_error_93: {
        state->error = 0x9;
        state->reason = "Invalid HTTP version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_107:
    s_n_llhttp__internal__n_error_107: {
        state->error = 0x9;
        state->reason = "Invalid minor version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_http_minor:
    s_n_llhttp__internal__n_res_http_minor: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_http_minor;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_store_http_minor_1;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_7;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_108:
    s_n_llhttp__internal__n_error_108: {
        state->error = 0x9;
        state->reason = "Expected dot";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_http_dot:
    s_n_llhttp__internal__n_res_http_dot: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_http_dot;
        }
        switch (*p) {
        case '.': {
            p++;
            goto s_n_llhttp__internal__n_res_http_minor;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_8;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_109:
    s_n_llhttp__internal__n_error_109: {
        state->error = 0x9;
        state->reason = "Invalid major version";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_http_major:
    s_n_llhttp__internal__n_res_http_major: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_http_major;
        }
        switch (*p) {
        case '0': {
            p++;
            match = 0;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '1': {
            p++;
            match = 1;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '2': {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '3': {
            p++;
            match = 3;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '4': {
            p++;
            match = 4;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '5': {
            p++;
            match = 5;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '6': {
            p++;
            match = 6;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '7': {
            p++;
            match = 7;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '8': {
            p++;
            match = 8;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        case '9': {
            p++;
            match = 9;
            goto s_n_llhttp__internal__n_invoke_store_http_major_1;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_version_9;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_version_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_version_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_version_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_version;
        goto s_n_llhttp__internal__n_res_http_major;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_protocol:
    s_n_llhttp__internal__n_res_after_protocol: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_protocol;
        }
        switch (*p) {
        case '/': {
            p++;
            goto s_n_llhttp__internal__n_span_start_llhttp__on_version_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_114;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_3:
    s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_3: {
        switch (llhttp__on_protocol_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_res_after_protocol;
        case 21:
            goto s_n_llhttp__internal__n_pause_30;
        default:
            goto s_n_llhttp__internal__n_error_113;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_error_115:
    s_n_llhttp__internal__n_error_115: {
        state->error = 0x8;
        state->reason = "Expected HTTP/, RTSP/ or ICE/";
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_error;
        return s_error;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_start_1:
    s_n_llhttp__internal__n_res_after_start_1: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_start_1;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob58, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_4;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_res_after_start_1;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_start_2:
    s_n_llhttp__internal__n_res_after_start_2: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_start_2;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob59, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_4;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_res_after_start_2;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_start_3:
    s_n_llhttp__internal__n_res_after_start_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_start_3;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob60, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_4;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_res_after_start_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_res_after_start:
    s_n_llhttp__internal__n_res_after_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_res_after_start;
        }
        switch (*p) {
        case 'H': {
            p++;
            goto s_n_llhttp__internal__n_res_after_start_1;
        }
        case 'I': {
            p++;
            goto s_n_llhttp__internal__n_res_after_start_2;
        }
        case 'R': {
            p++;
            goto s_n_llhttp__internal__n_res_after_start_3;
        }
        default: {
            goto s_n_llhttp__internal__n_span_end_llhttp__on_protocol_5;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_protocol_1:
    s_n_llhttp__internal__n_span_start_llhttp__on_protocol_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_protocol_1;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_protocol;
        goto s_n_llhttp__internal__n_res_after_start;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_llhttp__on_method_complete:
    s_n_llhttp__internal__n_invoke_llhttp__on_method_complete: {
        switch (llhttp__on_method_complete(state, p, endp)) {
        case 0:
            goto s_n_llhttp__internal__n_req_first_space_before_url;
        case 21:
            goto s_n_llhttp__internal__n_pause_26;
        default:
            goto s_n_llhttp__internal__n_error_1;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_or_res_method_2:
    s_n_llhttp__internal__n_req_or_res_method_2: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_or_res_method_2;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob61, 2);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            match = 2;
            goto s_n_llhttp__internal__n_invoke_store_method;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_or_res_method_2;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_110;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_update_type_1:
    s_n_llhttp__internal__n_invoke_update_type_1: {
        switch (llhttp__internal__c_update_type_1(state, p, endp)) {
        default:
            goto s_n_llhttp__internal__n_span_start_llhttp__on_version_1;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_or_res_method_3:
    s_n_llhttp__internal__n_req_or_res_method_3: {
        llparse_match_t match_seq;

        if (p == endp) {
            return s_n_llhttp__internal__n_req_or_res_method_3;
        }
        match_seq = llparse__match_sequence_id(state, p, endp, llparse_blob62, 3);
        p = match_seq.current;
        switch (match_seq.status) {
        case kMatchComplete: {
            p++;
            goto s_n_llhttp__internal__n_span_end_llhttp__on_method_1;
        }
        case kMatchPause: {
            return s_n_llhttp__internal__n_req_or_res_method_3;
        }
        case kMatchMismatch: {
            goto s_n_llhttp__internal__n_error_110;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_or_res_method_1:
    s_n_llhttp__internal__n_req_or_res_method_1: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_or_res_method_1;
        }
        switch (*p) {
        case 'E': {
            p++;
            goto s_n_llhttp__internal__n_req_or_res_method_2;
        }
        case 'T': {
            p++;
            goto s_n_llhttp__internal__n_req_or_res_method_3;
        }
        default: {
            goto s_n_llhttp__internal__n_error_110;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_req_or_res_method:
    s_n_llhttp__internal__n_req_or_res_method: {
        if (p == endp) {
            return s_n_llhttp__internal__n_req_or_res_method;
        }
        switch (*p) {
        case 'H': {
            p++;
            goto s_n_llhttp__internal__n_req_or_res_method_1;
        }
        default: {
            goto s_n_llhttp__internal__n_error_110;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_span_start_llhttp__on_method:
    s_n_llhttp__internal__n_span_start_llhttp__on_method: {
        if (p == endp) {
            return s_n_llhttp__internal__n_span_start_llhttp__on_method;
        }
        state->_span_pos0 = (void *)p;
        state->_span_cb0 = llhttp__on_method;
        goto s_n_llhttp__internal__n_req_or_res_method;
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_start_req_or_res:
    s_n_llhttp__internal__n_start_req_or_res: {
        if (p == endp) {
            return s_n_llhttp__internal__n_start_req_or_res;
        }
        switch (*p) {
        case 'H': {
            goto s_n_llhttp__internal__n_span_start_llhttp__on_method;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_update_type_2;
        }
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_load_type:
    s_n_llhttp__internal__n_invoke_load_type: {
        switch (llhttp__internal__c_load_type(state, p, endp)) {
        case 1:
            goto s_n_llhttp__internal__n_span_start_llhttp__on_method_1;
        case 2:
            goto s_n_llhttp__internal__n_span_start_llhttp__on_protocol_1;
        default:
            goto s_n_llhttp__internal__n_start_req_or_res;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_invoke_update_finish:
    s_n_llhttp__internal__n_invoke_update_finish: {
        switch (llhttp__internal__c_update_finish(state, p, endp)) {
        default:
            goto s_n_llhttp__internal__n_invoke_llhttp__on_message_begin;
        }
        UNREACHABLE;
    }
    case s_n_llhttp__internal__n_start:
    s_n_llhttp__internal__n_start: {
        if (p == endp) {
            return s_n_llhttp__internal__n_start;
        }
        switch (*p) {
        case 10: {
            p++;
            goto s_n_llhttp__internal__n_start;
        }
        case 13: {
            p++;
            goto s_n_llhttp__internal__n_start;
        }
        default: {
            goto s_n_llhttp__internal__n_invoke_load_initial_message_completed;
        }
        }
        UNREACHABLE;
    }
    default:
        UNREACHABLE;
    }
s_n_llhttp__internal__n_error_2: {
    state->error = 0x7;
    state->reason = "Invalid characters in url";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_finish_2: {
    switch (llhttp__internal__c_update_finish_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_start;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_initial_message_completed: {
    switch (llhttp__internal__c_update_initial_message_completed(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_finish_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_content_length: {
    switch (llhttp__internal__c_update_content_length(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_initial_message_completed;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_8: {
    state->error = 0x5;
    state->reason = "Data after `Connection: close`";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_3: {
    switch (llhttp__internal__c_test_lenient_flags_3(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_closed;
    default:
        goto s_n_llhttp__internal__n_error_8;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_2: {
    switch (llhttp__internal__c_test_lenient_flags_2(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_update_initial_message_completed;
    default:
        goto s_n_llhttp__internal__n_closed;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_finish_1: {
    switch (llhttp__internal__c_update_finish_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_13: {
    state->error = 0x15;
    state->reason = "on_message_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_is_equal_upgrade;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_38: {
    state->error = 0x12;
    state->reason = "`on_message_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_15: {
    state->error = 0x15;
    state->reason = "on_chunk_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_40: {
    state->error = 0x14;
    state->reason = "`on_chunk_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete_1: {
    switch (llhttp__on_chunk_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
    case 21:
        goto s_n_llhttp__internal__n_pause_15;
    default:
        goto s_n_llhttp__internal__n_error_40;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_2: {
    state->error = 0x15;
    state->reason = "on_message_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_pause_1;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_9: {
    state->error = 0x12;
    state->reason = "`on_message_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_1: {
    switch (llhttp__on_message_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_pause_1;
    case 21:
        goto s_n_llhttp__internal__n_pause_2;
    default:
        goto s_n_llhttp__internal__n_error_9;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_36: {
    state->error = 0xc;
    state->reason = "Chunk size overflow";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_10: {
    state->error = 0xc;
    state->reason = "Invalid character in chunk size";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_4: {
    switch (llhttp__internal__c_test_lenient_flags_4(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_chunk_size_otherwise;
    default:
        goto s_n_llhttp__internal__n_error_10;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_3: {
    state->error = 0x15;
    state->reason = "on_chunk_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_update_content_length_1;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_14: {
    state->error = 0x14;
    state->reason = "`on_chunk_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete: {
    switch (llhttp__on_chunk_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_update_content_length_1;
    case 21:
        goto s_n_llhttp__internal__n_pause_3;
    default:
        goto s_n_llhttp__internal__n_error_14;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_13: {
    state->error = 0x19;
    state->reason = "Missing expected CR after chunk data";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_6: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete;
    default:
        goto s_n_llhttp__internal__n_error_13;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_15: {
    state->error = 0x2;
    state->reason = "Expected LF after chunk data";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_7: {
    switch (llhttp__internal__c_test_lenient_flags_7(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete;
    default:
        goto s_n_llhttp__internal__n_error_15;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_body: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_body(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_data_almost_done;
        return s_error;
    }
    goto s_n_llhttp__internal__n_chunk_data_almost_done;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags: {
    switch (llhttp__internal__c_or_flags(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_field_start;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_4: {
    state->error = 0x15;
    state->reason = "on_chunk_header pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_is_equal_content_length;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_12: {
    state->error = 0x13;
    state->reason = "`on_chunk_header` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_chunk_header: {
    switch (llhttp__on_chunk_header(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_is_equal_content_length;
    case 21:
        goto s_n_llhttp__internal__n_pause_4;
    default:
        goto s_n_llhttp__internal__n_error_12;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_16: {
    state->error = 0x2;
    state->reason = "Expected LF after chunk size";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_8: {
    switch (llhttp__internal__c_test_lenient_flags_8(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_header;
    default:
        goto s_n_llhttp__internal__n_error_16;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_11: {
    state->error = 0x19;
    state->reason = "Missing expected CR after chunk size";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_5: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_chunk_size_almost_done;
    default:
        goto s_n_llhttp__internal__n_error_11;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_17: {
    state->error = 0x2;
    state->reason = "Invalid character in chunk extensions";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_18: {
    state->error = 0x2;
    state->reason = "Invalid character in chunk extensions";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_20: {
    state->error = 0x19;
    state->reason = "Missing expected CR after chunk extension name";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_5: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_name pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_test_lenient_flags_9;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_19: {
    state->error = 0x22;
    state->reason = "`on_chunk_extension_name` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_name(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_6: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_name pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_size_almost_done;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_21: {
    state->error = 0x22;
    state->reason = "`on_chunk_extension_name` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_name(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_1;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_7: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_name pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_extensions;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_22: {
    state->error = 0x22;
    state->reason = "`on_chunk_extension_name` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_name(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_2;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_2;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_25: {
    state->error = 0x19;
    state->reason = "Missing expected CR after chunk extension value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_8: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_value pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_test_lenient_flags_10;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_24: {
    state->error = 0x23;
    state->reason = "`on_chunk_extension_value` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_9: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_value pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_size_almost_done;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_26: {
    state->error = 0x23;
    state->reason = "`on_chunk_extension_value` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_1;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_28: {
    state->error = 0x19;
    state->reason = "Missing expected CR after chunk extension value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_11: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_chunk_size_almost_done;
    default:
        goto s_n_llhttp__internal__n_error_28;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_29: {
    state->error = 0x2;
    state->reason = "Invalid character in chunk extensions quote value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_10: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_value pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_extension_quoted_value_done;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_27: {
    state->error = 0x23;
    state->reason = "`on_chunk_extension_value` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_2;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_2;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_30;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_30;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_31;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_31;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_11: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_value pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_extensions;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_32: {
    state->error = 0x23;
    state->reason = "`on_chunk_extension_value` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_5: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_3;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_value_complete_3;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_value_6: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_33;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_33;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_12: {
    state->error = 0x15;
    state->reason = "on_chunk_extension_name pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_chunk_extension_value;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_23: {
    state->error = 0x22;
    state->reason = "`on_chunk_extension_name` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_chunk_extension_name_complete_3: {
    switch (llhttp__on_chunk_extension_name_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_chunk_extension_value;
    case 21:
        goto s_n_llhttp__internal__n_pause_12;
    default:
        goto s_n_llhttp__internal__n_error_23;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_name(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_span_start_llhttp__on_chunk_extension_value;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_chunk_extension_name_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_chunk_extension_name(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_34;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_34;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_35: {
    state->error = 0xc;
    state->reason = "Invalid character in chunk size";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_mul_add_content_length: {
    switch (llhttp__internal__c_mul_add_content_length(state, p, endp, match)) {
    case 1:
        goto s_n_llhttp__internal__n_error_36;
    default:
        goto s_n_llhttp__internal__n_chunk_size;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_37: {
    state->error = 0xc;
    state->reason = "Invalid character in chunk size";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_body_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_body(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_finish_3: {
    switch (llhttp__internal__c_update_finish_3(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_body_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_39: {
    state->error = 0xf;
    state->reason = "Request has invalid `Transfer-Encoding`";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause: {
    state->error = 0x15;
    state->reason = "on_message_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__after_message_complete;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_7: {
    state->error = 0x12;
    state->reason = "`on_message_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_message_complete: {
    switch (llhttp__on_message_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_message_complete;
    case 21:
        goto s_n_llhttp__internal__n_pause;
    default:
        goto s_n_llhttp__internal__n_error_7;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_1: {
    switch (llhttp__internal__c_or_flags_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_2: {
    switch (llhttp__internal__c_or_flags_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_upgrade: {
    switch (llhttp__internal__c_update_upgrade(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_or_flags_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_14: {
    state->error = 0x15;
    state->reason = "Paused by on_headers_complete";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_6: {
    state->error = 0x11;
    state->reason = "User callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_headers_complete: {
    switch (llhttp__on_headers_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    case 1:
        goto s_n_llhttp__internal__n_invoke_or_flags_1;
    case 2:
        goto s_n_llhttp__internal__n_invoke_update_upgrade;
    case 21:
        goto s_n_llhttp__internal__n_pause_14;
    default:
        goto s_n_llhttp__internal__n_error_6;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__before_headers_complete: {
    switch (llhttp__before_headers_complete(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags: {
    switch (llhttp__internal__c_test_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete_1;
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__before_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_1: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_flags;
    default:
        goto s_n_llhttp__internal__n_error_5;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_17: {
    state->error = 0x15;
    state->reason = "on_chunk_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_42: {
    state->error = 0x14;
    state->reason = "`on_chunk_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete_2: {
    switch (llhttp__on_chunk_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_message_complete_2;
    case 21:
        goto s_n_llhttp__internal__n_pause_17;
    default:
        goto s_n_llhttp__internal__n_error_42;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_3: {
    switch (llhttp__internal__c_or_flags_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_4: {
    switch (llhttp__internal__c_or_flags_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_upgrade_1: {
    switch (llhttp__internal__c_update_upgrade(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_or_flags_4;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_16: {
    state->error = 0x15;
    state->reason = "Paused by on_headers_complete";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_41: {
    state->error = 0x11;
    state->reason = "User callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_headers_complete_1: {
    switch (llhttp__on_headers_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_llhttp__after_headers_complete;
    case 1:
        goto s_n_llhttp__internal__n_invoke_or_flags_3;
    case 2:
        goto s_n_llhttp__internal__n_invoke_update_upgrade_1;
    case 21:
        goto s_n_llhttp__internal__n_pause_16;
    default:
        goto s_n_llhttp__internal__n_error_41;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__before_headers_complete_1: {
    switch (llhttp__before_headers_complete(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_headers_complete_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags_1: {
    switch (llhttp__internal__c_test_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_chunk_complete_2;
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__before_headers_complete_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_43: {
    state->error = 0x2;
    state->reason = "Expected LF after headers";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_12: {
    switch (llhttp__internal__c_test_lenient_flags_8(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_flags_1;
    default:
        goto s_n_llhttp__internal__n_error_43;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_44: {
    state->error = 0xa;
    state->reason = "Invalid header token";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_field: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_field(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_5;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_5;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_13: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_field_colon_discard_ws;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_field;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_60: {
    state->error = 0xb;
    state->reason = "Content-Length can't be present with Transfer-Encoding";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_47: {
    state->error = 0xa;
    state->reason = "Invalid header value char";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_15: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_value_discard_ws;
    default:
        goto s_n_llhttp__internal__n_error_47;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_49: {
    state->error = 0xb;
    state->reason = "Empty Content-Length";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_18: {
    state->error = 0x15;
    state->reason = "on_header_value_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_header_field_start;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_48: {
    state->error = 0x1d;
    state->reason = "`on_header_value_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state: {
    switch (llhttp__internal__c_update_header_state(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_5: {
    switch (llhttp__internal__c_or_flags_5(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_6: {
    switch (llhttp__internal__c_or_flags_6(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_7: {
    switch (llhttp__internal__c_or_flags_7(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_8: {
    switch (llhttp__internal__c_or_flags_8(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_2: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 5:
        goto s_n_llhttp__internal__n_invoke_or_flags_5;
    case 6:
        goto s_n_llhttp__internal__n_invoke_or_flags_6;
    case 7:
        goto s_n_llhttp__internal__n_invoke_or_flags_7;
    case 8:
        goto s_n_llhttp__internal__n_invoke_or_flags_8;
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_1: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 2:
        goto s_n_llhttp__internal__n_error_49;
    default:
        goto s_n_llhttp__internal__n_invoke_load_header_state_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_46: {
    state->error = 0xa;
    state->reason = "Invalid header value char";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_14: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_value_discard_lws;
    default:
        goto s_n_llhttp__internal__n_error_46;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_50: {
    state->error = 0x2;
    state->reason = "Expected LF after CR";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_16: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_value_discard_lws;
    default:
        goto s_n_llhttp__internal__n_error_50;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_1: {
    switch (llhttp__internal__c_update_header_state_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_4: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 8:
        goto s_n_llhttp__internal__n_invoke_update_header_state_1;
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_header_value_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_52: {
    state->error = 0xa;
    state->reason = "Unexpected whitespace after header value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_18: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_load_header_state_4;
    default:
        goto s_n_llhttp__internal__n_error_52;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_2: {
    switch (llhttp__internal__c_update_header_state(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_9: {
    switch (llhttp__internal__c_or_flags_5(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_10: {
    switch (llhttp__internal__c_or_flags_6(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_11: {
    switch (llhttp__internal__c_or_flags_7(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_12: {
    switch (llhttp__internal__c_or_flags_8(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_5: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 5:
        goto s_n_llhttp__internal__n_invoke_or_flags_9;
    case 6:
        goto s_n_llhttp__internal__n_invoke_or_flags_10;
    case 7:
        goto s_n_llhttp__internal__n_invoke_or_flags_11;
    case 8:
        goto s_n_llhttp__internal__n_invoke_or_flags_12;
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_header_value_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_53: {
    state->error = 0x3;
    state->reason = "Missing expected LF after header value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_51: {
    state->error = 0x19;
    state->reason = "Missing expected CR after header value";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_test_lenient_flags_17;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_test_lenient_flags_17;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_header_value_almost_done;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_header_value_almost_done;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_header_value_almost_done;
        return s_error;
    }
    goto s_n_llhttp__internal__n_header_value_almost_done;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_5: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_header_value_almost_done;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_header_value_almost_done;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_54;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_54;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_19: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_value_lenient;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_3;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_4: {
    switch (llhttp__internal__c_update_header_state(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_13: {
    switch (llhttp__internal__c_or_flags_5(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_4;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_14: {
    switch (llhttp__internal__c_or_flags_6(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_4;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_15: {
    switch (llhttp__internal__c_or_flags_7(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_4;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_16: {
    switch (llhttp__internal__c_or_flags_8(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_6: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 5:
        goto s_n_llhttp__internal__n_invoke_or_flags_13;
    case 6:
        goto s_n_llhttp__internal__n_invoke_or_flags_14;
    case 7:
        goto s_n_llhttp__internal__n_invoke_or_flags_15;
    case 8:
        goto s_n_llhttp__internal__n_invoke_or_flags_16;
    default:
        goto s_n_llhttp__internal__n_header_value_connection;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_5: {
    switch (llhttp__internal__c_update_header_state_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection_token;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_3: {
    switch (llhttp__internal__c_update_header_state_3(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_6: {
    switch (llhttp__internal__c_update_header_state_6(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_7: {
    switch (llhttp__internal__c_update_header_state_7(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_connection_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_6: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_56;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_56;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_mul_add_content_length_1: {
    switch (llhttp__internal__c_mul_add_content_length_1(state, p, endp, match)) {
    case 1:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_6;
    default:
        goto s_n_llhttp__internal__n_header_value_content_length;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_17: {
    switch (llhttp__internal__c_or_flags_17(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_otherwise;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_7: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_57;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_57;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_55: {
    state->error = 0x4;
    state->reason = "Duplicate Content-Length";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags_2: {
    switch (llhttp__internal__c_test_flags_2(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_header_value_content_length;
    default:
        goto s_n_llhttp__internal__n_error_55;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_9: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_59;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_59;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_8: {
    switch (llhttp__internal__c_update_header_state_8(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_otherwise;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_value_8: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_value(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_58;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_error_58;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_20: {
    switch (llhttp__internal__c_test_lenient_flags_20(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_8;
    default:
        goto s_n_llhttp__internal__n_header_value_te_chunked;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_type_1: {
    switch (llhttp__internal__c_load_type(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_20;
    default:
        goto s_n_llhttp__internal__n_header_value_te_chunked;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_9: {
    switch (llhttp__internal__c_update_header_state_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_and_flags: {
    switch (llhttp__internal__c_and_flags(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_value_te_chunked;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_19: {
    switch (llhttp__internal__c_or_flags_18(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_and_flags;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_21: {
    switch (llhttp__internal__c_test_lenient_flags_20(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_header_value_9;
    default:
        goto s_n_llhttp__internal__n_invoke_or_flags_19;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_type_2: {
    switch (llhttp__internal__c_load_type(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_21;
    default:
        goto s_n_llhttp__internal__n_invoke_or_flags_19;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_18: {
    switch (llhttp__internal__c_or_flags_18(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_and_flags;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags_3: {
    switch (llhttp__internal__c_test_flags_3(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_load_type_2;
    default:
        goto s_n_llhttp__internal__n_invoke_or_flags_18;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_or_flags_20: {
    switch (llhttp__internal__c_or_flags_20(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_header_state_9;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_header_state_3: {
    switch (llhttp__internal__c_load_header_state(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_value_connection;
    case 2:
        goto s_n_llhttp__internal__n_invoke_test_flags_2;
    case 3:
        goto s_n_llhttp__internal__n_invoke_test_flags_3;
    case 4:
        goto s_n_llhttp__internal__n_invoke_or_flags_20;
    default:
        goto s_n_llhttp__internal__n_header_value;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_22: {
    switch (llhttp__internal__c_test_lenient_flags_22(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_error_60;
    default:
        goto s_n_llhttp__internal__n_header_value_discard_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags_4: {
    switch (llhttp__internal__c_test_flags_4(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_22;
    default:
        goto s_n_llhttp__internal__n_header_value_discard_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_61: {
    state->error = 0xf;
    state->reason = "Transfer-Encoding can't be present with Content-Length";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_23: {
    switch (llhttp__internal__c_test_lenient_flags_22(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_error_61;
    default:
        goto s_n_llhttp__internal__n_header_value_discard_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_flags_5: {
    switch (llhttp__internal__c_test_flags_2(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_23;
    default:
        goto s_n_llhttp__internal__n_header_value_discard_ws;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_19: {
    state->error = 0x15;
    state->reason = "on_header_field_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_header_state;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_45: {
    state->error = 0x1c;
    state->reason = "`on_header_field_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_field_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_field(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_header_field_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_header_field(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_llhttp__on_header_field_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_62: {
    state->error = 0xa;
    state->reason = "Invalid header token";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_10: {
    switch (llhttp__internal__c_update_header_state_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_field_general;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_header_state: {
    switch (llhttp__internal__c_store_header_state(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_header_field_colon;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_header_state_11: {
    switch (llhttp__internal__c_update_header_state_1(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_header_field_general;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_4: {
    state->error = 0x1e;
    state->reason = "Unexpected space after start line";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags: {
    switch (llhttp__internal__c_test_lenient_flags(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_header_field_start;
    default:
        goto s_n_llhttp__internal__n_error_4;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_20: {
    state->error = 0x15;
    state->reason = "on_url_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_headers_start;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_3: {
    state->error = 0x1a;
    state->reason = "`on_url_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_url_complete: {
    switch (llhttp__on_url_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_headers_start;
    case 21:
        goto s_n_llhttp__internal__n_pause_20;
    default:
        goto s_n_llhttp__internal__n_error_3;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_http_minor: {
    switch (llhttp__internal__c_update_http_minor(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_url_complete;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_http_major: {
    switch (llhttp__internal__c_update_http_major(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_http_minor;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_63: {
    state->error = 0x7;
    state->reason = "Expected CRLF";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_lf_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_lf_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_72: {
    state->error = 0x17;
    state->reason = "Pause on PRI/Upgrade";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_73: {
    state->error = 0x9;
    state->reason = "Expected HTTP/2 Connection Preface";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_70: {
    state->error = 0x2;
    state->reason = "Expected CRLF after version";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_26: {
    switch (llhttp__internal__c_test_lenient_flags_8(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_headers_start;
    default:
        goto s_n_llhttp__internal__n_error_70;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_69: {
    state->error = 0x9;
    state->reason = "Expected CRLF after version";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_25: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_req_http_complete_crlf;
    default:
        goto s_n_llhttp__internal__n_error_69;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_71: {
    state->error = 0x9;
    state->reason = "Expected CRLF after version";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_21: {
    state->error = 0x15;
    state->reason = "on_version_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_method_1;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_68: {
    state->error = 0x21;
    state->reason = "`on_version_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_version_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_version_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_67;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_67;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 9:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_1;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor_1: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_1;
    case 1:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_1;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor_2: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_1;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_major: {
    switch (llhttp__internal__c_load_http_major(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_load_http_minor;
    case 1:
        goto s_n_llhttp__internal__n_invoke_load_http_minor_1;
    case 2:
        goto s_n_llhttp__internal__n_invoke_load_http_minor_2;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_24: {
    switch (llhttp__internal__c_test_lenient_flags_24(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_1;
    default:
        goto s_n_llhttp__internal__n_invoke_load_http_major;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_http_minor: {
    switch (llhttp__internal__c_store_http_minor(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_24;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_74;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_74;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_75;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_75;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_http_major: {
    switch (llhttp__internal__c_store_http_major(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_req_http_dot;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_76;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_76;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_77: {
    state->error = 0x8;
    state->reason = "Expected HTTP/, RTSP/ or ICE/";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_66: {
    state->error = 0x8;
    state->reason = "Invalid method for HTTP/x.x request";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_22: {
    state->error = 0x15;
    state->reason = "on_protocol_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_method;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_65: {
    state->error = 0x26;
    state->reason = "`on_protocol_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol_3: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_82;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_82;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_79: {
    state->error = 0x8;
    state->reason = "Expected SOURCE method for ICE/x.x request";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_23: {
    state->error = 0x15;
    state->reason = "on_protocol_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_method_2;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_78: {
    state->error = 0x26;
    state->reason = "`on_protocol_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_1;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_81: {
    state->error = 0x8;
    state->reason = "Invalid method for RTSP/x.x request";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_24: {
    state->error = 0x15;
    state->reason = "on_protocol_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_method_3;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_80: {
    state->error = 0x26;
    state->reason = "`on_protocol_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_2;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_2;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_25: {
    state->error = 0x15;
    state->reason = "on_url_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_req_http_start;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_64: {
    state->error = 0x1a;
    state->reason = "`on_url_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_url_complete_1: {
    switch (llhttp__on_url_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_req_http_start;
    case 21:
        goto s_n_llhttp__internal__n_pause_25;
    default:
        goto s_n_llhttp__internal__n_error_64;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_5: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_6: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_7: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_lf_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_lf_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_8: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_83: {
    state->error = 0x7;
    state->reason = "Invalid char in url fragment start";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_9: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_10: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_lf_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_lf_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_11: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_84: {
    state->error = 0x7;
    state->reason = "Invalid char in url query";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_85: {
    state->error = 0x7;
    state->reason = "Invalid char in url path";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_lf_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_lf_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_12: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_13: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_lf_to_http09;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_lf_to_http09;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_url_14: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_url(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_url_skip_to_http;
        return s_error;
    }
    goto s_n_llhttp__internal__n_url_skip_to_http;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_86: {
    state->error = 0x7;
    state->reason = "Double @ in url";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_87: {
    state->error = 0x7;
    state->reason = "Unexpected char in url server";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_88: {
    state->error = 0x7;
    state->reason = "Unexpected char in url server";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_89: {
    state->error = 0x7;
    state->reason = "Unexpected char in url schema";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_90: {
    state->error = 0x7;
    state->reason = "Unexpected char in url schema";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_91: {
    state->error = 0x7;
    state->reason = "Unexpected start char in url";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_is_equal_method: {
    switch (llhttp__internal__c_is_equal_method(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_url_entry_normal;
    default:
        goto s_n_llhttp__internal__n_url_entry_connect;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_92: {
    state->error = 0x6;
    state->reason = "Expected space after method";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_29: {
    state->error = 0x15;
    state->reason = "on_method_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_req_first_space_before_url;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_111: {
    state->error = 0x20;
    state->reason = "`on_method_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_method_2: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_method(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_method_complete_1;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_method_complete_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_method_1: {
    switch (llhttp__internal__c_store_method(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_method_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_112: {
    state->error = 0x6;
    state->reason = "Invalid method encountered";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_104: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_102: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_100: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_27: {
    state->error = 0x15;
    state->reason = "on_status_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_headers_start;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_96: {
    state->error = 0x1b;
    state->reason = "`on_status_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_status_complete: {
    switch (llhttp__on_status_complete(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_headers_start;
    case 21:
        goto s_n_llhttp__internal__n_pause_27;
    default:
        goto s_n_llhttp__internal__n_error_96;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_95: {
    state->error = 0xd;
    state->reason = "Invalid response status";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_28: {
    switch (llhttp__internal__c_test_lenient_flags_1(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_status_complete;
    default:
        goto s_n_llhttp__internal__n_error_95;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_97: {
    state->error = 0x2;
    state->reason = "Expected LF after CR";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_29: {
    switch (llhttp__internal__c_test_lenient_flags_8(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_status_complete;
    default:
        goto s_n_llhttp__internal__n_error_97;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_98: {
    state->error = 0x19;
    state->reason = "Missing expected CR after response line";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_status: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_status(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_test_lenient_flags_30;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_invoke_test_lenient_flags_30;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_status_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_status(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)(p + 1);
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_res_line_almost_done;
        return s_error;
    }
    p++;
    goto s_n_llhttp__internal__n_res_line_almost_done;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_99: {
    state->error = 0xd;
    state->reason = "Invalid response status";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_mul_add_status_code_2: {
    switch (llhttp__internal__c_mul_add_status_code(state, p, endp, match)) {
    case 1:
        goto s_n_llhttp__internal__n_error_100;
    default:
        goto s_n_llhttp__internal__n_res_status_code_otherwise;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_101: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_mul_add_status_code_1: {
    switch (llhttp__internal__c_mul_add_status_code(state, p, endp, match)) {
    case 1:
        goto s_n_llhttp__internal__n_error_102;
    default:
        goto s_n_llhttp__internal__n_res_status_code_digit_3;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_103: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_mul_add_status_code: {
    switch (llhttp__internal__c_mul_add_status_code(state, p, endp, match)) {
    case 1:
        goto s_n_llhttp__internal__n_error_104;
    default:
        goto s_n_llhttp__internal__n_res_status_code_digit_2;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_105: {
    state->error = 0xd;
    state->reason = "Invalid status code";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_status_code: {
    switch (llhttp__internal__c_update_status_code(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_res_status_code_digit_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_106: {
    state->error = 0x9;
    state->reason = "Expected space after version";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_28: {
    state->error = 0x15;
    state->reason = "on_version_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_res_after_version;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_94: {
    state->error = 0x21;
    state->reason = "`on_version_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_6: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_version_complete_1;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_version_complete_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_5: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_93;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_93;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor_3: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 9:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_6;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_5;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor_4: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_6;
    case 1:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_6;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_5;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_minor_5: {
    switch (llhttp__internal__c_load_http_minor(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_6;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_5;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_http_major_1: {
    switch (llhttp__internal__c_load_http_major(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_load_http_minor_3;
    case 1:
        goto s_n_llhttp__internal__n_invoke_load_http_minor_4;
    case 2:
        goto s_n_llhttp__internal__n_invoke_load_http_minor_5;
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_5;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_test_lenient_flags_27: {
    switch (llhttp__internal__c_test_lenient_flags_24(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_version_6;
    default:
        goto s_n_llhttp__internal__n_invoke_load_http_major_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_http_minor_1: {
    switch (llhttp__internal__c_store_http_minor(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_invoke_test_lenient_flags_27;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_7: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_107;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_107;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_8: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_108;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_108;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_http_major_1: {
    switch (llhttp__internal__c_store_http_major(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_res_http_dot;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_version_9: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_version(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_109;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_109;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_114: {
    state->error = 0x8;
    state->reason = "Expected HTTP/, RTSP/ or ICE/";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_30: {
    state->error = 0x15;
    state->reason = "on_protocol_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_res_after_protocol;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_113: {
    state->error = 0x26;
    state->reason = "`on_protocol_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol_4: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_3;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_protocol_complete_3;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_protocol_5: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_protocol(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_error_115;
        return s_error;
    }
    goto s_n_llhttp__internal__n_error_115;
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_26: {
    state->error = 0x15;
    state->reason = "on_method_complete pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_req_first_space_before_url;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_1: {
    state->error = 0x20;
    state->reason = "`on_method_complete` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_method: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_method(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_llhttp__on_method_complete;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_llhttp__on_method_complete;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_type: {
    switch (llhttp__internal__c_update_type(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_end_llhttp__on_method;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_store_method: {
    switch (llhttp__internal__c_store_method(state, p, endp, match)) {
    default:
        goto s_n_llhttp__internal__n_invoke_update_type;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_110: {
    state->error = 0x8;
    state->reason = "Invalid word encountered";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_span_end_llhttp__on_method_1: {
    const unsigned char *start;
    int err;

    start = state->_span_pos0;
    state->_span_pos0 = NULL;
    err = llhttp__on_method(state, start, p);
    if (err != 0) {
        state->error = err;
        state->error_pos = (const char *)p;
        state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_update_type_1;
        return s_error;
    }
    goto s_n_llhttp__internal__n_invoke_update_type_1;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_update_type_2: {
    switch (llhttp__internal__c_update_type(state, p, endp)) {
    default:
        goto s_n_llhttp__internal__n_span_start_llhttp__on_method_1;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_31: {
    state->error = 0x15;
    state->reason = "on_message_begin pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_load_type;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error: {
    state->error = 0x10;
    state->reason = "`on_message_begin` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_message_begin: {
    switch (llhttp__on_message_begin(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_load_type;
    case 21:
        goto s_n_llhttp__internal__n_pause_31;
    default:
        goto s_n_llhttp__internal__n_error;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_pause_32: {
    state->error = 0x15;
    state->reason = "on_reset pause";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_n_llhttp__internal__n_invoke_update_finish;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_error_116: {
    state->error = 0x1f;
    state->reason = "`on_reset` callback error";
    state->error_pos = (const char *)p;
    state->_current = (void *)(intptr_t)s_error;
    return s_error;
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_llhttp__on_reset: {
    switch (llhttp__on_reset(state, p, endp)) {
    case 0:
        goto s_n_llhttp__internal__n_invoke_update_finish;
    case 21:
        goto s_n_llhttp__internal__n_pause_32;
    default:
        goto s_n_llhttp__internal__n_error_116;
    }
    UNREACHABLE;
}
s_n_llhttp__internal__n_invoke_load_initial_message_completed: {
    switch (llhttp__internal__c_load_initial_message_completed(state, p, endp)) {
    case 1:
        goto s_n_llhttp__internal__n_invoke_llhttp__on_reset;
    default:
        goto s_n_llhttp__internal__n_invoke_update_finish;
    }
    UNREACHABLE;
}
}

int llhttp__internal_execute(llhttp__internal_t *state, const char *p, const char *endp)
{
    llparse_state_t next;

    /* check lingering errors */
    if (state->error != 0) {
        return state->error;
    }

    /* restart spans */
    if (state->_span_pos0 != NULL) {
        state->_span_pos0 = (void *)p;
    }

    next = llhttp__internal__run(state, (const unsigned char *)p, (const unsigned char *)endp);
    if (next == s_error) {
        return state->error;
    }
    state->_current = (void *)(intptr_t)next;

    /* execute spans */
    if (state->_span_pos0 != NULL) {
        int error;

        error = ((llhttp__internal__span_cb)state->_span_cb0)(state, state->_span_pos0, (const char *)endp);
        if (error != 0) {
            state->error = error;
            state->error_pos = endp;
            return error;
        }
    }

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// #include "llhttp.h"

#define CALLBACK_MAYBE(PARSER, NAME)                              \
    do {                                                          \
        const llhttp_settings_t *settings;                        \
        settings = (const llhttp_settings_t *)(PARSER)->settings; \
        if (settings == NULL || settings->NAME == NULL) {         \
            err = 0;                                              \
            break;                                                \
        }                                                         \
        err = settings->NAME((PARSER));                           \
    } while (0)

#define SPAN_CALLBACK_MAYBE(PARSER, NAME, START, LEN)                           \
    do {                                                                        \
        const llhttp_settings_t *settings;                                      \
        settings = (const llhttp_settings_t *)(PARSER)->settings;               \
        if (settings == NULL || settings->NAME == NULL) {                       \
            err = 0;                                                            \
            break;                                                              \
        }                                                                       \
        err = settings->NAME((PARSER), (START), (LEN));                         \
        if (err == -1) {                                                        \
            err = HPE_USER;                                                     \
            llhttp_set_error_reason((PARSER), "Span callback error in " #NAME); \
        }                                                                       \
    } while (0)

void llhttp_init(llhttp_t *parser, llhttp_type_t type,
                 const llhttp_settings_t *settings)
{
    llhttp__internal_init(parser);

    parser->type = type;
    parser->settings = (void *)settings;
}

#if defined(__wasm__)

extern int wasm_on_message_begin(llhttp_t *p);
extern int wasm_on_url(llhttp_t *p, const char *at, size_t length);
extern int wasm_on_status(llhttp_t *p, const char *at, size_t length);
extern int wasm_on_header_field(llhttp_t *p, const char *at, size_t length);
extern int wasm_on_header_value(llhttp_t *p, const char *at, size_t length);
extern int wasm_on_headers_complete(llhttp_t *p, int status_code,
                                    uint8_t upgrade, int should_keep_alive);
extern int wasm_on_body(llhttp_t *p, const char *at, size_t length);
extern int wasm_on_message_complete(llhttp_t *p);

static int wasm_on_headers_complete_wrap(llhttp_t *p)
{
    return wasm_on_headers_complete(p, p->status_code, p->upgrade,
                                    llhttp_should_keep_alive(p));
}

const llhttp_settings_t wasm_settings = {
    .on_message_begin = wasm_on_message_begin,
    .on_url = wasm_on_url,
    .on_status = wasm_on_status,
    .on_header_field = wasm_on_header_field,
    .on_header_value = wasm_on_header_value,
    .on_headers_complete = wasm_on_headers_complete_wrap,
    .on_body = wasm_on_body,
    .on_message_complete = wasm_on_message_complete,
};

llhttp_t *llhttp_alloc(llhttp_type_t type)
{
    llhttp_t *parser = malloc(sizeof(llhttp_t));
    llhttp_init(parser, type, &wasm_settings);
    return parser;
}

void llhttp_free(llhttp_t *parser)
{
    free(parser);
}

#endif // defined(__wasm__)

/* Some getters required to get stuff from the parser */

uint8_t llhttp_get_type(llhttp_t *parser)
{
    return parser->type;
}

uint8_t llhttp_get_http_major(llhttp_t *parser)
{
    return parser->http_major;
}

uint8_t llhttp_get_http_minor(llhttp_t *parser)
{
    return parser->http_minor;
}

uint8_t llhttp_get_method(llhttp_t *parser)
{
    return parser->method;
}

int llhttp_get_status_code(llhttp_t *parser)
{
    return parser->status_code;
}

uint8_t llhttp_get_upgrade(llhttp_t *parser)
{
    return parser->upgrade;
}

void llhttp_reset(llhttp_t *parser)
{
    llhttp_type_t type = parser->type;
    const llhttp_settings_t *settings = parser->settings;
    void *data = parser->data;
    uint16_t lenient_flags = parser->lenient_flags;

    llhttp__internal_init(parser);

    parser->type = type;
    parser->settings = (void *)settings;
    parser->data = data;
    parser->lenient_flags = lenient_flags;
}

llhttp_errno_t llhttp_execute(llhttp_t *parser, const char *data, size_t len)
{
    return llhttp__internal_execute(parser, data, data + len);
}

void llhttp_settings_init(llhttp_settings_t *settings)
{
    memset(settings, 0, sizeof(*settings));
}

llhttp_errno_t llhttp_finish(llhttp_t *parser)
{
    int err;

    /* We're in an error state. Don't bother doing anything. */
    if (parser->error != 0) {
        return 0;
    }

    switch (parser->finish) {
    case HTTP_FINISH_SAFE_WITH_CB:
        CALLBACK_MAYBE(parser, on_message_complete);
        if (err != HPE_OK)
            return err;

    /* FALLTHROUGH */
    case HTTP_FINISH_SAFE:
        return HPE_OK;
    case HTTP_FINISH_UNSAFE:
        parser->reason = "Invalid EOF state";
        return HPE_INVALID_EOF_STATE;
    default:
        abort();
    }
}

void llhttp_pause(llhttp_t *parser)
{
    if (parser->error != HPE_OK) {
        return;
    }

    parser->error = HPE_PAUSED;
    parser->reason = "Paused";
}

void llhttp_resume(llhttp_t *parser)
{
    if (parser->error != HPE_PAUSED) {
        return;
    }

    parser->error = 0;
}

void llhttp_resume_after_upgrade(llhttp_t *parser)
{
    if (parser->error != HPE_PAUSED_UPGRADE) {
        return;
    }

    parser->error = 0;
}

llhttp_errno_t llhttp_get_errno(const llhttp_t *parser)
{
    return parser->error;
}

const char *llhttp_get_error_reason(const llhttp_t *parser)
{
    return parser->reason;
}

void llhttp_set_error_reason(llhttp_t *parser, const char *reason)
{
    parser->reason = reason;
}

const char *llhttp_get_error_pos(const llhttp_t *parser)
{
    return parser->error_pos;
}

const char *llhttp_errno_name(llhttp_errno_t err)
{
#define HTTP_ERRNO_GEN(CODE, NAME, _) \
    case HPE_##NAME:                  \
        return "HPE_" #NAME;
    switch (err) {
        HTTP_ERRNO_MAP(HTTP_ERRNO_GEN)
    default:
        abort();
    }
#undef HTTP_ERRNO_GEN
}

const char *llhttp_method_name(llhttp_method_t method)
{
#define HTTP_METHOD_GEN(NUM, NAME, STRING) \
    case HTTP_##NAME:                      \
        return #STRING;
    switch (method) {
        HTTP_ALL_METHOD_MAP(HTTP_METHOD_GEN)
    default:
        abort();
    }
#undef HTTP_METHOD_GEN
}

const char *llhttp_status_name(llhttp_status_t status)
{
#define HTTP_STATUS_GEN(NUM, NAME, STRING) \
    case HTTP_STATUS_##NAME:               \
        return #STRING;
    switch (status) {
        HTTP_STATUS_MAP(HTTP_STATUS_GEN)
    default:
        abort();
    }
#undef HTTP_STATUS_GEN
}

void llhttp_set_lenient_headers(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_HEADERS;
    } else {
        parser->lenient_flags &= ~LENIENT_HEADERS;
    }
}

void llhttp_set_lenient_chunked_length(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_CHUNKED_LENGTH;
    } else {
        parser->lenient_flags &= ~LENIENT_CHUNKED_LENGTH;
    }
}

void llhttp_set_lenient_keep_alive(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_KEEP_ALIVE;
    } else {
        parser->lenient_flags &= ~LENIENT_KEEP_ALIVE;
    }
}

void llhttp_set_lenient_transfer_encoding(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_TRANSFER_ENCODING;
    } else {
        parser->lenient_flags &= ~LENIENT_TRANSFER_ENCODING;
    }
}

void llhttp_set_lenient_version(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_VERSION;
    } else {
        parser->lenient_flags &= ~LENIENT_VERSION;
    }
}

void llhttp_set_lenient_data_after_close(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_DATA_AFTER_CLOSE;
    } else {
        parser->lenient_flags &= ~LENIENT_DATA_AFTER_CLOSE;
    }
}

void llhttp_set_lenient_optional_lf_after_cr(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_OPTIONAL_LF_AFTER_CR;
    } else {
        parser->lenient_flags &= ~LENIENT_OPTIONAL_LF_AFTER_CR;
    }
}

void llhttp_set_lenient_optional_crlf_after_chunk(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_OPTIONAL_CRLF_AFTER_CHUNK;
    } else {
        parser->lenient_flags &= ~LENIENT_OPTIONAL_CRLF_AFTER_CHUNK;
    }
}

void llhttp_set_lenient_optional_cr_before_lf(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_OPTIONAL_CR_BEFORE_LF;
    } else {
        parser->lenient_flags &= ~LENIENT_OPTIONAL_CR_BEFORE_LF;
    }
}

void llhttp_set_lenient_spaces_after_chunk_size(llhttp_t *parser, int enabled)
{
    if (enabled) {
        parser->lenient_flags |= LENIENT_SPACES_AFTER_CHUNK_SIZE;
    } else {
        parser->lenient_flags &= ~LENIENT_SPACES_AFTER_CHUNK_SIZE;
    }
}

/* Callbacks */

int llhttp__on_message_begin(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_message_begin);
    return err;
}

int llhttp__on_protocol(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_protocol, p, endp - p);
    return err;
}

int llhttp__on_protocol_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_protocol_complete);
    return err;
}

int llhttp__on_url(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_url, p, endp - p);
    return err;
}

int llhttp__on_url_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_url_complete);
    return err;
}

int llhttp__on_status(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_status, p, endp - p);
    return err;
}

int llhttp__on_status_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_status_complete);
    return err;
}

int llhttp__on_method(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_method, p, endp - p);
    return err;
}

int llhttp__on_method_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_method_complete);
    return err;
}

int llhttp__on_version(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_version, p, endp - p);
    return err;
}

int llhttp__on_version_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_version_complete);
    return err;
}

int llhttp__on_header_field(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_header_field, p, endp - p);
    return err;
}

int llhttp__on_header_field_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_header_field_complete);
    return err;
}

int llhttp__on_header_value(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_header_value, p, endp - p);
    return err;
}

int llhttp__on_header_value_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_header_value_complete);
    return err;
}

int llhttp__on_headers_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_headers_complete);
    return err;
}

int llhttp__on_message_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_message_complete);
    return err;
}

int llhttp__on_body(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_body, p, endp - p);
    return err;
}

int llhttp__on_chunk_header(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_chunk_header);
    return err;
}

int llhttp__on_chunk_extension_name(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_chunk_extension_name, p, endp - p);
    return err;
}

int llhttp__on_chunk_extension_name_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_chunk_extension_name_complete);
    return err;
}

int llhttp__on_chunk_extension_value(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    SPAN_CALLBACK_MAYBE(s, on_chunk_extension_value, p, endp - p);
    return err;
}

int llhttp__on_chunk_extension_value_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_chunk_extension_value_complete);
    return err;
}

int llhttp__on_chunk_complete(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_chunk_complete);
    return err;
}

int llhttp__on_reset(llhttp_t *s, const char *p, const char *endp)
{
    int err;
    CALLBACK_MAYBE(s, on_reset);
    return err;
}

/* Private */

void llhttp__debug(llhttp_t *s, const char *p, const char *endp,
                   const char *msg)
{
    if (p == endp) {
        fprintf(stderr, "p=%p type=%d flags=%02x next=null debug=%s\n", s, s->type,
                s->flags, msg);
    } else {
        fprintf(stderr, "p=%p type=%d flags=%02x next=%02x   debug=%s\n", s,
                s->type, s->flags, *p, msg);
    }
}

#include <stdio.h>
#ifndef LLHTTP__TEST
// # include "llhttp.h"
#else
#define llhttp_t llparse_t
#endif /* */

int llhttp_message_needs_eof(const llhttp_t *parser);
int llhttp_should_keep_alive(const llhttp_t *parser);

int llhttp__before_headers_complete(llhttp_t *parser, const char *p,
                                    const char *endp)
{
    /* Set this here so that on_headers_complete() callbacks can see it */
    if ((parser->flags & F_UPGRADE) &&
        (parser->flags & F_CONNECTION_UPGRADE)) {
        /* For responses, "Upgrade: foo" and "Connection: upgrade" are
         * mandatory only when it is a 101 Switching Protocols response,
         * otherwise it is purely informational, to announce support.
         */
        parser->upgrade =
            (parser->type == HTTP_REQUEST || parser->status_code == 101);
    } else {
        parser->upgrade = (parser->method == HTTP_CONNECT);
    }
    return 0;
}

/* Return values:
 * 0 - No body, `restart`, message_complete
 * 1 - CONNECT request, `restart`, message_complete, and pause
 * 2 - chunk_size_start
 * 3 - body_identity
 * 4 - body_identity_eof
 * 5 - invalid transfer-encoding for request
 */
int llhttp__after_headers_complete(llhttp_t *parser, const char *p,
                                   const char *endp)
{
    int hasBody;

    hasBody = parser->flags & F_CHUNKED || parser->content_length > 0;
    if (
        (parser->upgrade && (parser->method == HTTP_CONNECT ||
                             (parser->flags & F_SKIPBODY) || !hasBody)) ||
        /* See RFC 2616 section 4.4 - 1xx e.g. Continue */
        (parser->type == HTTP_RESPONSE && parser->status_code == 101)) {
        /* Exit, the rest of the message is in a different protocol. */
        return 1;
    }

    if (parser->type == HTTP_RESPONSE && parser->status_code == 100) {
        /* No body, restart as the message is complete */
        return 0;
    }

    /* See RFC 2616 section 4.4 */
    if (
        parser->flags & F_SKIPBODY || /* response to a HEAD request */
        (
            parser->type == HTTP_RESPONSE && (parser->status_code == 102 || /* Processing */
                                              parser->status_code == 103 || /* Early Hints */
                                              parser->status_code == 204 || /* No Content */
                                              parser->status_code == 304    /* Not Modified */
                                              ))) {
        return 0;
    } else if (parser->flags & F_CHUNKED) {
        /* chunked encoding - ignore Content-Length header, prepare for a chunk */
        return 2;
    } else if (parser->flags & F_TRANSFER_ENCODING) {
        if (parser->type == HTTP_REQUEST &&
            (parser->lenient_flags & LENIENT_CHUNKED_LENGTH) == 0 &&
            (parser->lenient_flags & LENIENT_TRANSFER_ENCODING) == 0) {
            /* RFC 7230 3.3.3 */

            /* If a Transfer-Encoding header field
             * is present in a request and the chunked transfer coding is not
             * the final encoding, the message body length cannot be determined
             * reliably; the server MUST respond with the 400 (Bad Request)
             * status code and then close the connection.
             */
            return 5;
        } else {
            /* RFC 7230 3.3.3 */

            /* If a Transfer-Encoding header field is present in a response and
             * the chunked transfer coding is not the final encoding, the
             * message body length is determined by reading the connection until
             * it is closed by the server.
             */
            return 4;
        }
    } else {
        if (!(parser->flags & F_CONTENT_LENGTH)) {
            if (!llhttp_message_needs_eof(parser)) {
                /* Assume content-length 0 - read the next */
                return 0;
            } else {
                /* Read body until EOF */
                return 4;
            }
        } else if (parser->content_length == 0) {
            /* Content-Length header given but zero: Content-Length: 0\r\n */
            return 0;
        } else {
            /* Content-Length header given and non-zero */
            return 3;
        }
    }
}

int llhttp__after_message_complete(llhttp_t *parser, const char *p,
                                   const char *endp)
{
    int should_keep_alive;

    should_keep_alive = llhttp_should_keep_alive(parser);
    parser->finish = HTTP_FINISH_SAFE;
    parser->flags = 0;

    /* NOTE: this is ignored in loose parsing mode */
    return should_keep_alive;
}

int llhttp_message_needs_eof(const llhttp_t *parser)
{
    if (parser->type == HTTP_REQUEST) {
        return 0;
    }

    /* See RFC 2616 section 4.4 */
    if (parser->status_code / 100 == 1 || /* 1xx e.g. Continue */
        parser->status_code == 204 ||     /* No Content */
        parser->status_code == 304 ||     /* Not Modified */
        (parser->flags & F_SKIPBODY)) {   /* response to a HEAD request */
        return 0;
    }

    /* RFC 7230 3.3.3, see `llhttp__after_headers_complete` */
    if ((parser->flags & F_TRANSFER_ENCODING) &&
        (parser->flags & F_CHUNKED) == 0) {
        return 1;
    }

    if (parser->flags & (F_CHUNKED | F_CONTENT_LENGTH)) {
        return 0;
    }

    return 1;
}

int llhttp_should_keep_alive(const llhttp_t *parser)
{
    if (parser->http_major > 0 && parser->http_minor > 0) {
        /* HTTP/1.1 */
        if (parser->flags & F_CONNECTION_CLOSE) {
            return 0;
        }
    } else {
        /* HTTP/1.0 or earlier */
        if (!(parser->flags & F_CONNECTION_KEEP_ALIVE)) {
            return 0;
        }
    }

    return !llhttp_message_needs_eof(parser);
}

/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////

// LCOV_EXCL_STOP
