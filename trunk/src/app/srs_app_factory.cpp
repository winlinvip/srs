//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_factory.hpp>

#include <srs_app_config.hpp>
#include <srs_app_st.hpp>
#include <srs_protocol_st.hpp>

SrsFinalFactory::SrsFinalFactory()
{
}

SrsFinalFactory::~SrsFinalFactory()
{
}

ISrsCoroutine *SrsFinalFactory::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler, SrsContextId cid)
{
    return new SrsSTCoroutine(name, handler, cid);
}

ISrsTime *SrsFinalFactory::create_time()
{
    return new SrsTrueTime();
}

ISrsConfig *SrsFinalFactory::create_config()
{
    return new SrsConfigProxy();
}

ISrsCond *SrsFinalFactory::create_cond()
{
    return new SrsCond();
}

SrsConfigProxy::SrsConfigProxy()
{
}

SrsConfigProxy::~SrsConfigProxy()
{
}

srs_utime_t SrsConfigProxy::get_pithy_print()
{
    return _srs_config->get_pithy_print();
}

std::string SrsConfigProxy::get_default_app_name()
{
    return _srs_config->get_default_app_name();
}

SrsTrueTime::SrsTrueTime()
{
}

SrsTrueTime::~SrsTrueTime()
{
}

void SrsTrueTime::usleep(srs_utime_t duration)
{
    srs_usleep(duration);
}
