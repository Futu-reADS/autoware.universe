// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2015, 2016, 2017, 2019, 2020, 2021, 2022 Rutger van Beusekom <rutger@dezyne.org>
// Copyright © 2017, 2018, 2019, 2020, 2021, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_CONTAINER_HH
#define DZN_CONTAINER_HH

#include <dzn/config.hh>
#include <dzn/locator.hh>
#include <dzn/runtime.hh>
#include <dzn/pump.hh>

#include <algorithm>
#include <functional>
#include <iostream>
#include <iterator>
#include <map>
#include <queue>
#include <sstream>
#include <string>

namespace dzn
{
template <typename System, typename Function>
struct container: public component
{
  dzn::meta dzn_meta;
  dzn::locator dzn_locator;
  dzn::runtime dzn_runtime;
  System system;

  std::map<std::string, Function> lookup;
  std::queue<std::string> trail;
  std::mutex mutex;
  std::condition_variable condition;
  dzn::pump pump;

  friend std::ostream &operator << (std::ostream &os, container<System, Function> &)
  {
    return os;
  }

  container (bool flush, dzn::locator &&l = dzn::locator{})
    : dzn_meta{"<external>", "container", 0, {}, {&system.dzn_meta}, {[this]{dzn::check_bindings (system);}}}
    , dzn_locator (std::forward<dzn::locator> (l))
    , dzn_runtime ()
    , system (dzn_locator.set (dzn_runtime).set (pump))
    , pump ()
  {
    dzn_locator.get<illegal_handler> ().illegal = [] (const char *location = "") {std::clog << location << (location[0] ? ":0: " : "") << "<illegal>" << std::endl; std::exit (0);};
    dzn_runtime.performs_flush (this) = flush;
    system.dzn_meta.name = "sut";
  }
  ~container ()
  {
    std::unique_lock<std::mutex> lock (mutex);
    condition.wait (lock, [this] {return trail.empty ();});
    dzn::pump *p = system.dzn_locator.template try_get<dzn::pump> (); // only shells have a pump
    //resolve the race condition between the shell pump dtor and the container pump dtor
    if (p && p != &pump) pump ([p] {p->stop ();});
    pump.wait ();
  }
  void perform (const std::string &str)
  {
    if (std::count (str.begin (), str.end (), '.') > 1
        || str == "<defer>")
      return;

    auto it = lookup.find (str);
    if (it != lookup.end ())
      pump (it->second);

    std::unique_lock<std::mutex> lock (mutex);
    trail.push (str);
    condition.notify_one ();
  }
  void operator () (std::map<std::string, Function> &&lookup)
  {
    this->lookup = std::move (lookup);
    pump.pause ();
    std::string str;
    while (std::getline (std::cin, str))
      {
        if (str.find ("<flush>") != std::string::npos
            || str == "<defer>")
          pump.flush ();
        perform (str);
      }
    pump.resume ();
  }
  void match (const std::string &perform)
  {
    std::string expect = trail_expect ();
    if (expect != perform)
      throw std::runtime_error ("unmatched expectation: trail expects: \"" + expect
                                + "\" behavior expects: \"" + perform + "\"");
  }
  std::string trail_expect ()
  {
    std::unique_lock<std::mutex> lock (mutex);
    condition.wait_for (lock, std::chrono::seconds (10),
                        [this] {return trail.size ();});
    std::string expect = trail.front ();
    trail.pop ();
    if (trail.empty ())
      condition.notify_one ();
    return expect;
  }
};
}

#endif //DZN_CONTAINER_HH
//version: 2.18.0
