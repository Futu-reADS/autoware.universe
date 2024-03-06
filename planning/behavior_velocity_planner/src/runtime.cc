// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2014, 2015, 2016, 2017, 2019, 2020, 2021, 2022 Jan (janneke) Nieuwenhuizen <janneke@gnu.org>
// Copyright © 2015, 2016, 2017, 2019, 2020, 2021, 2022 Rutger van Beusekom <rutger@dezyne.org>
// Copyright © 2015 Paul Hoogendijk <paul@dezyne.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#include <dzn/runtime.hh>
#include <dzn/coroutine.hh>

#include <algorithm>
#include <iostream>

namespace dzn
{
std::ostream debug (nullptr);
runtime::runtime () {}

void trace_in (std::ostream &os, port::meta const &meta, const char *event_name)
{
  if (!os.rdbuf ())
    return;
  os << path (meta.require.meta, meta.require.name) << "." << event_name << " -> "
     << path (meta.provide.meta, meta.provide.name) << "." << event_name << std::endl;
}

void trace_out (std::ostream &os, port::meta const &meta, const char *event_name)
{
  if (!os.rdbuf ())
    return;
  os << path (meta.require.meta, meta.require.name) << "." << event_name << " <- "
     << path (meta.provide.meta, meta.provide.name) << "." << event_name << std::endl;
}

void trace_qin (std::ostream &os, port::meta const &meta, const char *event_name)
{
  if (!os.rdbuf ())
    return;
  if (path (meta.require.meta, "") == "<external>")
    trace_out (os, meta, event_name);
  else
    os <<  path (meta.require.meta, "<q>")
       << " <- "
       << path (meta.provide.meta, meta.provide.name) << "." << event_name << std::endl;
}

void trace_qout (std::ostream &os, port::meta const &meta, const char *event_name)
{
  if (!os.rdbuf ())
    return;
  if (path (meta.require.meta, "") == "<external>")
    return;
  os << path (meta.require.meta, meta.require.name) << "." << event_name
     << " <- "
     << path (meta.require.meta, "<q>") << std::endl;
}

bool runtime::external (dzn::component *component)
{
  return (states.find (component) == states.end ());
}

size_t &runtime::handling (dzn::component *component)
{
  return states[component].handling;
}

size_t &runtime::blocked (dzn::component *component)
{
  return states[component].blocked;
}

dzn::component *&runtime::deferred (dzn::component *component)
{
  return states[component].deferred;
}

std::queue<std::function<void ()> > &runtime::queue (dzn::component *component)
{
  return states[component].queue;
}

bool &runtime::performs_flush (dzn::component *component)
{
  return states[component].performs_flush;
}

bool runtime::skip_block (dzn::component *component, void *port)
{
  return states.at (component).skip == port;
}

void runtime::set_skip_block (dzn::component *component, void *port)
{
  states[component].skip = port;
}

void runtime::reset_skip_block (dzn::component *component)
{
  states.at (component).skip = nullptr;
}

void runtime::flush (dzn::component *component, size_t coroutine_id)
{
  handling (component) = 0;
  if (!external (component))
    {
      std::queue<std::function<void ()> > &q = queue (component);
      while (! q.empty ())
        {
          std::function<void ()> event = q.front ();
          q.pop ();
          handling (component) = coroutine_id;
          event ();
          handling (component) = 0;
        }
      if (deferred (component))
        {
          dzn::component *tgt = deferred (component);
          deferred (component) = nullptr;
          if (!handling (tgt))
            {
              runtime::flush (tgt, coroutine_id);
            }
        }
    }
}
bool runtime::queue_p (dzn::component *source, dzn::component *target)
{
  return (source && performs_flush (source))
    || handling (target);
}
void runtime::enqueue (dzn::component *source, dzn::component *target,
                       const std::function<void ()> &event, size_t coroutine_id)
{
  if (queue_p (source, target))
    {
      deferred (source) = target;
      queue (target).push (event);
    }
  else
    {
      handling (target) = coroutine_id;
      event ();
      handling (target) = 0;
      flush (target, coroutine_id);
    }
}
}
//version: 2.18.0
