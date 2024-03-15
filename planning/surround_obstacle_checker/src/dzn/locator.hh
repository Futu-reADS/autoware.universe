// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2016, 2019, 2020, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
// Copyright © 2017, 2022, 2023 Rutger van Beusekom <rutger@dezyne.org>
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

#ifndef DZN_LOCATOR_HH
#define DZN_LOCATOR_HH

#include <dzn/config.hh>

#include <cassert>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace dzn
{
struct illegal_handler
{
  std::function<void (char const *)> illegal = [] (char const *location = "")
  {
    std::cout << location << (location[0] ? ": " : "") << "<illegal>" << std::endl;
    assert (!"illegal");
  };
  [[noreturn]] void handle (char const *msg = "")
  {
    illegal (msg);
    std::abort ();
  }
};

struct locator
{
private:
  typedef std::string Key;
  struct type_info
  {
    const std::type_info *type;
    type_info (std::type_info const& t)
      : type (&t)
    {}
    bool operator < (type_info const& that) const
    {
      return type->before (*that.type);
    }
  };
  std::map<std::pair<Key, type_info>, const void *> services;
  locator (locator const&) = default;
public:
  locator (locator &&) = default;
  locator ()
  {
    static illegal_handler ih;
    set (std::clog).set (ih);
  }
  locator clone () const
  {
    return locator (*this);
  }
  template <typename T>
  locator &set (T &t, const Key &key = Key ())
  {
    services[std::make_pair (key, type_info (typeid (T)))] = &t;
    return *this;
  }
  template <typename T>
  T *try_get (Key const& key = Key ()) const
  {
    auto it = services.find (std::make_pair (key, type_info (typeid (T))));
    if (it != services.end () && it->second)
      return reinterpret_cast<T *> (const_cast<void *> (it->second));
    return nullptr;
  }
  template <typename T>
  T &get (Key const& key = Key ()) const
  {
    if (T *t = try_get<T> (key))
      return *t;
    throw std::runtime_error ("<" + std::string (typeid (T).name ()) + ",\"" + key + "\"> not available");
  }
};
}
#endif //DZN_LOCATOR_HH
