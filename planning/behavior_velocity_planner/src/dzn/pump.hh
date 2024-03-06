// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2016, 2017, 2019, 2020, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
// Copyright © 2016 Henk Katerberg <hank@mudball.nl>
// Copyright © 2016, 2017, 2018, 2019, 2021, 2022 Rutger van Beusekom <rutger@dezyne.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_PUMP_HH
#define DZN_PUMP_HH

#include <dzn/config.hh>
#include <dzn/coroutine.hh>
#include <dzn/meta.hh>

#include <condition_variable>
#include <functional>
#include <future>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <thread>
#include <vector>

namespace dzn
{
extern std::ostream debug;

struct runtime;

struct pump
{
  std::vector<void *> unblocked;
  bool running;
  bool paused;
  std::function<void ()> worker;
  std::list<coroutine> coroutines;
  std::list<coroutine> collateral_blocked;
  size_t current_coroutine;
  std::queue<std::function<void ()>> queue;
  std::vector<std::pair<std::function<bool ()>, std::function<void (size_t)>>> deferred;

  struct deadline
  {
    size_t id;
    std::chrono::steady_clock::time_point t;
    deadline (size_t id, size_t ms)
      : id (id)
      , t (std::chrono::steady_clock::now () + std::chrono::milliseconds (ms))
    {}
    bool expired () const {return t <= std::chrono::steady_clock::now ();}
    bool operator < (const deadline &d) const
    {return t < d.t || (t == d.t && id < d.id);}
  };

  std::map<deadline, std::function<void ()>> timers;
  std::vector<std::function<void ()>> switch_context;
  std::function<void ()> exit;
  std::thread::id thread_id;
  std::condition_variable condition;
  std::condition_variable idle;
  std::mutex mutex;
  std::future<void> task;
  pump ();
  ~pump ();
  size_t coroutine_id ();
  void stop ();
  void wait ();
  void pause ();
  void resume ();
  void flush ();
  void operator () ();

  void collateral_block (runtime &, dzn::component *);
  void collateral_release (std::list<coroutine>::iterator);

  bool blocked_p (void *);
  void block (runtime &, dzn::component *, void *);
  bool collateral_release_skip_block (dzn::component *);
  void create_context ();
  void context_switch ();
  void release (runtime &, dzn::component *, void *);
  void operator () (const std::function<void ()> &);
  void operator () (std::function<void ()> &&);
  void defer (std::function<bool ()> &&, std::function<void (size_t)> &&);
  void prune_deferred ();
  void handle (size_t, size_t, const std::function<void ()> &);
  void remove (size_t);
private:
  bool timers_expired () const;
};

#if __cplusplus > 201402L
template <typename L, typename ... Args, typename = typename std::enable_if<std::is_void<typename std::invoke_result<L, Args ...>::type>::value>::type>
#else
template <typename L, typename ... Args, typename = typename std::enable_if<std::is_void<typename std::result_of<L (Args ...)>::type>::value>::type>
#endif
void shell (dzn::pump &pump, L && l, Args && ...args)
{
  std::promise<void> p;
  pump ([&] {l (std::forward<Args> (args)...); p.set_value ();});
  return p.get_future ().get ();
}
#if __cplusplus > 201402L
template < typename L, typename ... Args, typename = typename std::enable_if < !std::is_void<typename std::invoke_result<L,Args ...>::type>::value >::type >
#else
template < typename L, typename ... Args, typename = typename std::enable_if < !std::is_void<typename std::result_of<L (Args ...)>::type>::value >::type >
#endif
auto shell (dzn::pump &pump, L && l, Args && ...args) -> decltype (l (std::forward<Args> (args)...))
{
  std::promise<decltype (l (std::forward<Args> (args)...))> p;
  pump ([&] {p.set_value (l (std::forward<Args> (args)...));});
  return p.get_future ().get ();
}
}

#endif //DZN_PUMP_HH
//version: 2.18.0
