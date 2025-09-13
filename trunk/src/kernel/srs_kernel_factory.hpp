//
// Copyright (c) 2013-2025 The SRS Authors
//
// SPDX-License-Identifier: MIT
//

#ifndef SRS_KERNEL_FACTORY_HPP
#define SRS_KERNEL_FACTORY_HPP

#include <srs_core.hpp>

#include <srs_kernel_st.hpp>
#include <srs_core_time.hpp>

// The factory to create kernel objects.
class ISrsKernelFactory
{
public:
    ISrsKernelFactory();
    virtual ~ISrsKernelFactory();

  public:
    virtual ISrsCoroutine *create_coroutine(const std::string &name, ISrsCoroutineHandler *handler) = 0;
    virtual ISrsTime *create_time() = 0;
};

extern ISrsKernelFactory *_srs_kernel_factory;

#endif
