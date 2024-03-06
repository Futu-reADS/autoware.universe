// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2023 Rutger van Beusekom <rutger@dezyne.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_STD_ASYNC_HH
#define DZN_STD_ASYNC_HH

#include <functional>
#include <future>

// forward declaration of dzn::async as indirection for std::async or
// dzn::thread::pool::defer

namespace dzn
{
std::future<void> std_async (std::function<void ()> const &);
}

#endif
//version: 2.18.0
