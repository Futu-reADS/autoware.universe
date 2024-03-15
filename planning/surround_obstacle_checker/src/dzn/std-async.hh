// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2023 Rutger van Beusekom <rutger@dezyne.org>
//
// This file is part of dzn-runtime.
//
// dzn-runtime is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// dzn-runtime is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with dzn-runtime.  If not, see <http://www.gnu.org/licenses/>.
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
