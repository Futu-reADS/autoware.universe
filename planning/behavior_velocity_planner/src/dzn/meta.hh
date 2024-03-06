// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2018, 2020, 2021, 2022, 2023 Rutger van Beusekom <rutger@dezyne.org>
// Copyright © 2019, 2020, 2021, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_META_HH
#define DZN_META_HH

#include <dzn/config.hh>

#include <algorithm>
#include <functional>
#include <map>
#include <string>
#include <stdexcept>
#include <vector>

namespace dzn
{
#define DZN_VERSION_STRING "2.18.0"
int const version_major = 2;
int const version_minor = 18;
int const version_patch = 0;

  struct meta;
  struct component {};

  namespace port
  {
    struct meta
    {
      struct
      {
        std::string name;
        void* port;
        dzn::component* component;
        const dzn::meta* meta;
      } provide;

      struct
      {
        std::string name;
        void* port;
        dzn::component* component;
        const dzn::meta* meta;
      } require;
    };
    template <typename Port>
    void* other(Port& port)
    {
      return (&port == port.dzn_meta.provide.port)
        ? port.dzn_meta.require.port : port.dzn_meta.provide.port;
    }
  }

  struct meta
  {
    std::string name;
    std::string type;
    const meta* parent;
    std::vector<const port::meta*> require;
    std::vector<const meta*> children;
    std::vector<std::function<void()>> ports_connected;
  };

  struct component_meta
  {
    meta dzn_meta;
  };

  inline std::string path(const meta* m, std::string p = std::string())
  {
    p = p.empty() ? p : "." + p;
    if(!m) return "<external>" + p;
    if(!m->parent) return m->name + p;
    return path(m->parent, m->name + p);
  }

  struct binding_error: public std::runtime_error
  {
    binding_error(const port::meta& m, const std::string& msg)
    : std::runtime_error("not connected: " + path(m.provide.component ? m.provide.meta : m.require.meta,
                                                  m.provide.component ? m.provide.name : m.require.name) + "." + msg)
    {}
  };
}
#endif //DZN_META_HH
//version: 2.18.0
