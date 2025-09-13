//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#include <srs_app_factory.hpp>

#include <srs_app_st.hpp>
#include <srs_protocol_st.hpp>

SrsFinalFactory::SrsFinalFactory()
{
}

SrsFinalFactory::~SrsFinalFactory()
{
}

ISrsCoroutine *SrsFinalFactory::create_coroutine(const std::string &name, ISrsCoroutineHandler *handler)
{
    return new SrsSTCoroutine(name, handler);
}

ISrsTime *SrsFinalFactory::create_time()
{
    return new SrsTrueTime();
}

