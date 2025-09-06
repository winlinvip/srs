//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_BALANCE_HPP
#define SRS_KERNEL_BALANCE_HPP

#include <srs_core.hpp>

#include <string>
#include <vector>

/**
 * Interface for round-robin load balance algorithm.
 * Used for edge pull and other multiple server feature.
 */
class ISrsLbRoundRobin
{
public:
    ISrsLbRoundRobin();
    virtual ~ISrsLbRoundRobin();

public:
    // Select one server from the servers.
    virtual std::string select(const std::vector<std::string> &servers) = 0;
};

/**
 * the round-robin load balance algorithm,
 * used for edge pull and other multiple server feature.
 */
class SrsLbRoundRobin : public ISrsLbRoundRobin
{
private:
    // current selected index.
    int index;
    // total scheduled count.
    uint32_t count;
    // current selected server.
    std::string elem;

public:
    SrsLbRoundRobin();
    virtual ~SrsLbRoundRobin();

public:
    virtual uint32_t current();
    virtual std::string selected();
    virtual std::string select(const std::vector<std::string> &servers);
};

#endif
