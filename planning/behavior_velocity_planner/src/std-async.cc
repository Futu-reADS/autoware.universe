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

#include <functional>
#include <future>

namespace dzn
{
std::future<void> std_async (std::function<void ()> const &work)
{
  return std::async (std::launch::async, work);
}
}
//version: 2.18.0
