// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2016, 2017, 2023 Jan Nieuwenhuizen <janneke@gnu.org>
// Copyright © 2016 Henk Katerberg <hank@mudball.nl>
// Copyright © 2015, 2016, 2017, 2018, 2022 Rutger van Beusekom <rutger@dezyne.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_COROUTINE_HH
#define DZN_COROUTINE_HH

#include <dzn/config.hh>

#if HAVE_BOOST_COROUTINE
#include <boost/coroutine/all.hpp>
#else
#include <dzn/context.hh>
#endif

namespace dzn
{
#if HAVE_BOOST_COROUTINE
typedef boost::coroutines::symmetric_coroutine<void>::call_type context;
typedef boost::coroutines::symmetric_coroutine<void>::yield_type yield;
typedef boost::coroutines::detail::forced_unwind forced_unwind;
#else
typedef context::forced_unwind forced_unwind;
typedef std::function<void (dzn::context &)> yield;
#endif

struct coroutine
{
  size_t id;
  dzn::context context;
  dzn::yield yield;
  void *component;
  void *port;
  bool finished;
  bool skip_block;
  template <typename Worker>
  coroutine (size_t id, Worker &&worker)
    : id (id)
    , context ([this, worker] (dzn::yield & yield)
    {
      this->yield = std::move (yield);
      worker ();
    })
    , port ()
    , finished ()
    , skip_block ()
  {}
  coroutine ()
    : id (0)
    , context ()
    , port ()
    , finished ()
    , skip_block ()
  {}
  void yield_to (dzn::coroutine &c)
  {
    this->yield (c.context);
  }
#if HAVE_BOOST_COROUTINE
  void call (dzn::coroutine &)
  {
    this->context ();
  }
  void release () {}
#else //!HAVE_BOOST_COROUTINE
  void call (dzn::coroutine &c)
  {
    this->context.call (c.context);
  }
  void release ()
  {
    this->context.release ();
  }
#endif // !HAVE_BOOST_COROUTINE
};
}
#endif //DZN_COROUTINE_HH
//version: 2.18.0
