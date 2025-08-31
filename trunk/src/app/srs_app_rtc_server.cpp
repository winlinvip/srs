//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_rtc_server.hpp>

#include <netdb.h>

#include <set>
using namespace std;

#include <srs_app_config.hpp>
#include <srs_app_http_api.hpp>
#include <srs_app_pithy_print.hpp>
#include <srs_app_rtc_api.hpp>
#include <srs_app_rtc_conn.hpp>
#include <srs_app_rtc_dtls.hpp>
#include <srs_app_rtc_network.hpp>
#include <srs_app_rtc_source.hpp>
#include <srs_app_server.hpp>
#include <srs_app_statistic.hpp>
#include <srs_app_utility.hpp>
#include <srs_core_autofree.hpp>
#include <srs_kernel_error.hpp>
#include <srs_kernel_log.hpp>
#include <srs_kernel_utility.hpp>
#include <srs_protocol_http_stack.hpp>
#include <srs_protocol_log.hpp>
#include <srs_protocol_rtc_stun.hpp>
#include <srs_protocol_utility.hpp>

extern SrsPps *_srs_pps_rpkts;
extern SrsPps *_srs_pps_rstuns;
extern SrsPps *_srs_pps_rrtps;
extern SrsPps *_srs_pps_rrtcps;
extern SrsPps *_srs_pps_addrs;
extern SrsPps *_srs_pps_fast_addrs;

extern SrsPps *_srs_pps_spkts;
extern SrsPps *_srs_pps_sstuns;
extern SrsPps *_srs_pps_srtcps;
extern SrsPps *_srs_pps_srtps;

extern SrsPps *_srs_pps_ids;
extern SrsPps *_srs_pps_fids;
extern SrsPps *_srs_pps_fids_level0;

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

// TODO: FIXME: Replace by ST dns resolve.
string srs_dns_resolve(string host, int &family)
{
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = family;

    addrinfo *r_raw = NULL;
    if (getaddrinfo(host.c_str(), NULL, &hints, &r_raw)) {
        return "";
    }
    SrsUniquePtr<addrinfo> r(r_raw, freeaddrinfo);

    char shost[64];
    memset(shost, 0, sizeof(shost));
    if (getnameinfo(r->ai_addr, r->ai_addrlen, shost, sizeof(shost), NULL, 0, NI_NUMERICHOST)) {
        return "";
    }

    family = r->ai_family;
    return string(shost);
}

SrsRtcBlackhole::SrsRtcBlackhole()
{
    blackhole = false;
    blackhole_addr = NULL;
    blackhole_stfd = NULL;
}

SrsRtcBlackhole::~SrsRtcBlackhole()
{
    srs_close_stfd(blackhole_stfd);
    srs_freep(blackhole_addr);
}

srs_error_t SrsRtcBlackhole::initialize()
{
    srs_error_t err = srs_success;

    blackhole = _srs_config->get_rtc_server_black_hole();
    if (!blackhole) {
        return err;
    }

    string blackhole_ep = _srs_config->get_rtc_server_black_hole_addr();
    if (blackhole_ep.empty()) {
        blackhole = false;
        srs_warn("disable black hole for no endpoint");
        return err;
    }

    string host;
    int port;
    srs_net_split_hostport(blackhole_ep, host, port);

    srs_freep(blackhole_addr);
    blackhole_addr = new sockaddr_in();
    blackhole_addr->sin_family = AF_INET;
    blackhole_addr->sin_addr.s_addr = inet_addr(host.c_str());
    blackhole_addr->sin_port = htons(port);

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    blackhole_stfd = srs_netfd_open_socket(fd);
    srs_assert(blackhole_stfd);

    srs_trace("RTC blackhole %s:%d, fd=%d", host.c_str(), port, fd);

    return err;
}

void SrsRtcBlackhole::sendto(void *data, int len)
{
    if (!blackhole) {
        return;
    }

    // For blackhole, we ignore any error.
    srs_sendto(blackhole_stfd, data, len, (sockaddr *)blackhole_addr, sizeof(sockaddr_in), SRS_UTIME_NO_TIMEOUT);
}

SrsRtcBlackhole *_srs_blackhole = NULL;

// @global dtls certficate for rtc module.
SrsDtlsCertificate *_srs_rtc_dtls_certificate = NULL;

// TODO: Should support error response.
// For STUN packet, 0x00 is binding request, 0x01 is binding success response.
bool srs_is_stun(const uint8_t *data, size_t size)
{
    return size > 0 && (data[0] == 0 || data[0] == 1);
}

// change_cipher_spec(20), alert(21), handshake(22), application_data(23)
// @see https://tools.ietf.org/html/rfc2246#section-6.2.1
bool srs_is_dtls(const uint8_t *data, size_t len)
{
    return (len >= 13 && (data[0] > 19 && data[0] < 64));
}

// For RTP or RTCP, the V=2 which is in the high 2bits, 0xC0 (1100 0000)
bool srs_is_rtp_or_rtcp(const uint8_t *data, size_t len)
{
    return (len >= 12 && (data[0] & 0xC0) == 0x80);
}

// For RTCP, PT is [128, 223] (or without marker [0, 95]).
// Literally, RTCP starts from 64 not 0, so PT is [192, 223] (or without marker [64, 95]).
// @note For RTP, the PT is [96, 127], or [224, 255] with marker.
bool srs_is_rtcp(const uint8_t *data, size_t len)
{
    return (len >= 12) && (data[0] & 0x80) && (data[1] >= 192 && data[1] <= 223);
}

srs_error_t api_server_as_candidates(string api, set<string> &candidate_ips)
{
    srs_error_t err = srs_success;

    if (api.empty() || !_srs_config->get_api_as_candidates()) {
        return err;
    }

    string hostname = api;
    if (hostname.empty() || hostname == SRS_CONSTS_LOCALHOST_NAME) {
        return err;
    }
    if (hostname == SRS_CONSTS_LOCALHOST || hostname == SRS_CONSTS_LOOPBACK || hostname == SRS_CONSTS_LOOPBACK6) {
        return err;
    }

    // Whether add domain name.
    if (!srs_net_is_ipv4(hostname) && _srs_config->get_keep_api_domain()) {
        candidate_ips.insert(hostname);
    }

    // Try to parse the domain name if not IP.
    if (!srs_net_is_ipv4(hostname) && _srs_config->get_resolve_api_domain()) {
        int family = 0;
        string ip = srs_dns_resolve(hostname, family);
        if (ip.empty() || ip == SRS_CONSTS_LOCALHOST || ip == SRS_CONSTS_LOOPBACK || ip == SRS_CONSTS_LOOPBACK6) {
            return err;
        }

        // Try to add the API server ip as candidates.
        candidate_ips.insert(ip);
    }

    // If hostname is IP, use it.
    if (srs_net_is_ipv4(hostname)) {
        candidate_ips.insert(hostname);
    }

    return err;
}

set<string> discover_candidates(SrsRtcUserConfig *ruc)
{
    srs_error_t err = srs_success;

    // Try to discover the eip as candidate, specified by user.
    set<string> candidate_ips;
    if (!ruc->eip_.empty()) {
        candidate_ips.insert(ruc->eip_);
    }

    // Try to discover from api of request, if api_as_candidates enabled.
    if ((err = api_server_as_candidates(ruc->req_->host, candidate_ips)) != srs_success) {
        srs_warn("ignore discovering ip from api %s, err %s", ruc->req_->host.c_str(), srs_error_summary(err).c_str());
        srs_freep(err);
    }

    // If not * or 0.0.0.0, use the candidate as exposed IP.
    string candidate = _srs_config->get_rtc_server_candidates();
    if (candidate != "*" && candidate != "0.0.0.0") {
        candidate_ips.insert(candidate);
        return candidate_ips;
    }

    // All automatically detected IP list.
    vector<SrsIPAddress *> &ips = srs_get_local_ips();
    if (ips.empty()) {
        return candidate_ips;
    }

    // Discover from local network interface addresses.
    if (_srs_config->get_use_auto_detect_network_ip()) {
        // We try to find the best match candidates, no loopback.
        string family = _srs_config->get_rtc_server_ip_family();
        for (int i = 0; i < (int)ips.size(); ++i) {
            SrsIPAddress *ip = ips[i];
            if (ip->is_loopback) {
                continue;
            }

            if (family == "ipv4" && !ip->is_ipv4) {
                continue;
            }
            if (family == "ipv6" && ip->is_ipv4) {
                continue;
            }

            candidate_ips.insert(ip->ip);
            srs_trace("Best matched ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        }
    }

    if (!candidate_ips.empty()) {
        return candidate_ips;
    }

    // Then, we use the ipv4 address.
    for (int i = 0; i < (int)ips.size(); ++i) {
        SrsIPAddress *ip = ips[i];
        if (!ip->is_ipv4) {
            continue;
        }

        candidate_ips.insert(ip->ip);
        srs_trace("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    // We use the first one, to make sure there will be at least one CANDIDATE.
    if (candidate_ips.empty()) {
        SrsIPAddress *ip = ips[0];
        candidate_ips.insert(ip->ip);
        srs_warn("No best matched, use first ip=%s, ifname=%s", ip->ip.c_str(), ip->ifname.c_str());
        return candidate_ips;
    }

    return candidate_ips;
}

SrsRtcUserConfig::SrsRtcUserConfig()
{
    req_ = new SrsRequest();
    publish_ = false;
    dtls_ = srtp_ = true;
    audio_before_video_ = false;
}

SrsRtcUserConfig::~SrsRtcUserConfig()
{
    srs_freep(req_);
}
