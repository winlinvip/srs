//
// Copyright (c) 2013-2026 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_UTEST_AI54_HPP
#define SRS_UTEST_AI54_HPP

/*
#include <srs_utest_ai54.hpp>
*/
#include <srs_utest.hpp>

#include <srs_app_edge.hpp>
#include <srs_app_http_source.hpp>
#include <srs_utest_ai53.hpp>

// The config of a vhost that remuxes its streams to HTTP, as http_remux turns it on, on an origin.
class MockAppConfigForHttpSource : public MockAppConfigForStreamBridges
{
public:
    bool http_remux_;
    bool reduce_sequence_header_;

public:
    MockAppConfigForHttpSource();
    virtual ~MockAppConfigForHttpSource();

public:
    virtual bool get_vhost_http_remux_enabled(std::string vhost);
    virtual bool get_reduce_sequence_header(std::string vhost);
};

// The HTTP server, which mounts the HTTP stream of a source when it publishes.
class MockHttpSourceHandler : public ISrsHttpSourceHandler
{
public:
    int nb_mounts_;
    int nb_unmounts_;

public:
    MockHttpSourceHandler();
    virtual ~MockHttpSourceHandler();

public:
    virtual srs_error_t http_mount(ISrsRequest *r);
    virtual void http_unmount(ISrsRequest *r);
};

// The pull of an edge from its origin, which counts how often the edge starts and stops it.
class MockEdgePullForHttpPlayers : public ISrsEdgeIngester
{
public:
    int nb_starts_;
    int nb_stops_;

public:
    MockEdgePullForHttpPlayers();
    virtual ~MockEdgePullForHttpPlayers();

public:
    virtual srs_error_t initialize(SrsSharedPtr<SrsRtmpSource> s, ISrsPlayEdge *e, ISrsRequest *r);
    virtual srs_error_t start();
    virtual void stop();
};

// Replaces the pull of the source's play edge by the mock, which the edge then owns.
extern void mock_edge_pull_for_http_players(SrsRtmpSource *source, MockEdgePullForHttpPlayers *pull);

#endif
