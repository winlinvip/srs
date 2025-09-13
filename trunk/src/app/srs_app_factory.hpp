//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_APP_FACTORY_HPP
#define SRS_APP_FACTORY_HPP

#include <srs_core.hpp>

#include <srs_kernel_factory.hpp>

// The factory to create kernel objects.
class SrsFinalFactory : public ISrsKernelFactory
{
public:
    SrsFinalFactory();
    virtual ~SrsFinalFactory();

public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid);
    virtual ISrsTime *create_time();
    virtual ISrsConfig *create_config();
    virtual ISrsCond *create_cond();
};

// The proxy for config.
class SrsConfigProxy : public ISrsConfig
{
public:
    SrsConfigProxy();
    virtual ~SrsConfigProxy();

public:
    virtual srs_utime_t get_pithy_print();
    virtual std::string get_default_app_name();
};

// The time to use system time.
class SrsTrueTime : public ISrsTime
{
public:
    SrsTrueTime();
    virtual ~SrsTrueTime();

public:
    virtual void usleep(srs_utime_t duration);
};

#endif
