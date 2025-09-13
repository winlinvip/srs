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
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler);
    virtual ISrsTime *create_time();
};

#endif
