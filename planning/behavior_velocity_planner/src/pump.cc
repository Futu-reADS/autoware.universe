// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2015, 2016, 2017, 2018, 2019, 2021, 2022, 2023 Rutger van Beusekom <rutger@dezyne.org>
// Copyright © 2016 Rob Wieringa <rma.wieringa@gmail.com>
// Copyright © 2016 Henk Katerberg <hank@mudball.nl>
// Copyright © 2015, 2016, 2017, 2019, 2020, 2022, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#include <dzn/std-async.hh>
#include <dzn/locator.hh>
#include <dzn/meta.hh>
#include <dzn/pump.hh>
#include <dzn/runtime.hh>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <thread>

namespace dzn
{
static std::list<coroutine>::iterator find_self (std::list<coroutine> &coroutines);
void defer (const dzn::locator &locator, std::function<bool ()> &&predicate,
            std::function<void (size_t)> &&event)
{
  locator.get<dzn::pump> ().defer (std::move (predicate), std::move (event));
}
void prune_deferred (const dzn::locator &locator)
{
  if (auto pump = locator.try_get<dzn::pump> ())
    pump->prune_deferred ();
}
size_t coroutine_id (const dzn::locator &locator)
{
  auto pump = locator.try_get<dzn::pump> ();
  return !pump ? 1 : pump->coroutine_id ();
}
void port_block (const dzn::locator &locator, dzn::component *component, void *port)
{
  dzn::debug.rdbuf () && dzn::debug << "port_block " << component
                                    << " " << port << std::endl;
  locator.get<dzn::pump> ().block (locator.get<dzn::runtime> (), component, port);
}
void port_release (const dzn::locator &locator, dzn::component *component, void *port)
{
  dzn::debug.rdbuf () && dzn::debug << "port_release " << component
                                    << " " << port << std::endl;
  locator.get<dzn::pump> ().release (locator.get<dzn::runtime> (), component, port);
}
void collateral_block (const dzn::locator &locator, dzn::component *component)
{
  dzn::debug.rdbuf () && dzn::debug << "pump::handling "
                                    << locator.get<dzn::runtime>().handling (component)
                                    << std::endl;
  dzn::debug.rdbuf () && dzn::debug << "collateral_block " << component << std::endl;
  locator.get<dzn::pump> ().collateral_block (locator.get<dzn::runtime> (), component);
}
bool port_blocked_p (const dzn::locator &locator, void *port)
{
  dzn::pump *pump = locator.try_get<dzn::pump> ();
  return pump ? pump->blocked_p (port) : false;
}

static std::list<coroutine>::iterator find_self (std::list<coroutine> &coroutines)
{
  auto coroutine_p = [] (const dzn::coroutine & coroutine)
  {return coroutine.port == nullptr && !coroutine.finished;};

#ifndef NDEBUG
  size_t count = std::count_if (coroutines.begin (), coroutines.end (), coroutine_p);
  assert (count != 0);
  assert (count != 2);
  assert (count < 3);
  assert (count == 1);
#endif
  return std::find_if (coroutines.begin (), coroutines.end (), coroutine_p);
}

static std::list<coroutine>::iterator find_blocked (std::list<coroutine> &coroutines,
                                                    void *port)
{
  return std::find_if (coroutines.begin (), coroutines.end (),
                       [port] (dzn::coroutine & c) {return c.port == port;});
}

static void remove_finished_coroutines (std::list<coroutine> &coroutines)
{
  coroutines.remove_if ([] (dzn::coroutine & c)
  {
    if (c.finished) debug.rdbuf () && debug << "[" << c.id << "] removing"
                                            << std::endl;
    return c.finished;
  });
}


pump::pump ()
  : unblocked ()
  , running (true)
  , paused (false)
  , current_coroutine (0)
  , switch_context ()
  , task (dzn::std_async (std::ref (*this)))
{}
pump::~pump ()
{
  stop ();
}
bool pump::blocked_p (void *port)
{
  return find_blocked (coroutines, port) != coroutines.end ();
}
void pump::stop ()
{
  debug.rdbuf () &&debug << "pump::stop" << std::endl;
  std::unique_lock<std::mutex> lock (mutex);
  if (running)
    {
      running = false;
      condition.notify_one ();
      lock.unlock ();
      task.wait ();
    }
}
void pump::wait ()
{
  debug.rdbuf () &&debug << "pump::wait" << std::endl;
  std::unique_lock<std::mutex> lock (mutex);
  idle.wait (lock, [this] {return queue.empty () && deferred.empty ();});
}
void pump::pause ()
{
  debug.rdbuf () &&debug << "pump::pause" << std::endl;
  std::unique_lock<std::mutex> lock (mutex);
  paused = true;
}
void pump::resume ()
{
  debug.rdbuf () &&debug << "pump::resume" << std::endl;
  std::unique_lock<std::mutex> lock (mutex);
  paused = false;
  condition.notify_one ();
}
void pump::flush ()
{
  debug.rdbuf () &&debug << "pump::flush" << std::endl;
  resume ();
  std::this_thread::sleep_for (std::chrono::milliseconds (100));
  pause ();
}
void pump::operator () ()
{
  try
    {
      auto work_p = [this] {return queue.size () || deferred.size () || !running;};

      worker = [&]
      {
        std::unique_lock<std::mutex> lock (mutex);
        if (queue.empty ())
          idle.notify_one ();

        if (queue.empty () && deferred.empty ())
          {
            if (timers.size ())
              condition.wait_until (lock, timers.begin ()->first.t, work_p);
            else
              condition.wait (lock, work_p);
          }

        if (queue.size ())
          {
            std::function<void ()> f (std::move (queue.front ()));
            queue.pop ();
            lock.unlock ();
            f ();
            lock.lock ();
          }

        if (queue.empty () && deferred.size () && deferred.front ().first ())
          {
            auto p = deferred.front ();
            deferred.erase (deferred.begin ());
            lock.unlock ();
            p.second (current_coroutine);
            lock.lock ();
          }

        while (timers_expired ())
          {
            auto f (timers.begin ()->second);
            timers.erase (timers.begin ());
            lock.unlock ();
            f ();
            lock.lock ();
          }
      };

      coroutine zero;
      debug.rdbuf () &&debug << "coroutine zero: "
                             << zero.id << std::endl;
      create_context ();
      debug.rdbuf () &&debug << "coroutine self: "
                             << find_self (coroutines)->id << std::endl;

      exit = [&]
      {
        debug.rdbuf () &&debug << "enter exit"
                               << std::endl;
        zero.release ();
      };

      std::unique_lock<std::mutex> lock (mutex);
      while (running || queue.size () || collateral_blocked.size ())
        {
          condition.wait (lock, [this] {return !paused;});
          lock.unlock ();
          assert (coroutines.size ());
          coroutines.back ().call (zero);
          lock.lock ();
          remove_finished_coroutines (coroutines);
        }
      debug.rdbuf () &&debug << "finish pump; #coroutines: " << coroutines.size ()
                             << " #collateral: " << collateral_blocked.size () << std::endl;

      for (auto &coroutine : coroutines)
        debug.rdbuf () && debug << coroutine.id << ":"
                                << coroutine.finished << std::endl;

      assert (queue.empty ());
      assert (coroutines.size () != 0);
      assert (coroutines.size () != 2);
      assert (coroutines.size () < 3);
      assert (coroutines.size () == 1);
    }
  catch (const std::exception &exception)
    {
      debug.rdbuf () &&debug << "oops: " << exception.what () << std::endl;
      std::terminate ();
    }
}
bool pump::timers_expired () const
{
  return timers.size () && timers.begin ()->first.expired ();
}
size_t pump::coroutine_id ()
{
  return find_self (coroutines)->id;
}
void pump::create_context ()
{
  coroutines.emplace_back (++current_coroutine, [&]
  {
    try
      {
        auto self = find_self (coroutines);
        debug.rdbuf () &&debug << "[" << self->id << "] create context"
                               << std::endl;
        context_switch ();
        while (running || queue.size () || timers_expired ())
          {
            worker ();
            if (unblocked.size ()) collateral_release (self);
            context_switch ();
          }
        exit ();
      }
    catch (const forced_unwind &)
      {
        debug.rdbuf () &&debug << "ignoring forced_unwind"
                               << std::endl;
      }
    catch (const std::exception &e)
      {
        debug.rdbuf () &&debug << "oops: " << e.what ()
                               << std::endl;
        std::terminate ();
      }
  });
}
void pump::context_switch ()
{
  if (switch_context.size ())
    {
      debug.rdbuf () &&debug << "context_switch" << std::endl;
      auto context = std::move (switch_context.front ());
      switch_context.erase (switch_context.begin ());
      context ();
    }
}
void pump::collateral_block (dzn::runtime &runtime, dzn::component *component)
{
  auto self = find_self (coroutines);
  debug.rdbuf () &&debug << "[" << self->id << "] collateral_block"
                         << std::endl;

  collateral_blocked.splice (collateral_blocked.end (), coroutines, self);

  self->component = component;
  size_t coroutine_id = runtime.handling (component) | runtime.blocked (component);
  auto it = find_if (coroutines.begin (), coroutines.end (),
                     [coroutine_id] (dzn::coroutine & coroutine)
                     {return coroutine.id == coroutine_id;});

  if (it == coroutines.end () || !it->port)
    throw std::runtime_error ("blocking port not found");

  self->port = it->port;

  debug.rdbuf () &&debug << "[" << self->id << "] collateral block on "
                         << self->port << std::endl;

  create_context ();
  self->yield_to (coroutines.back ());

  debug.rdbuf () &&debug << "[" << self->id << "] collateral_unblock"
                         << std::endl;
}
void pump::collateral_release (std::list<coroutine>::iterator self)
{
  debug.rdbuf () &&debug << "[" << self->id << "] collateral_release"
                         << std::endl;

  auto predicate = [this] (const coroutine & c)
  {
    return std::find (unblocked.begin (),
                      unblocked.end (),
                      c.port) != unblocked.end ();
  };

  auto it = collateral_blocked.end ();
  do
    {
      it = std::find_if (collateral_blocked.begin (),
                         collateral_blocked.end (),
                         predicate);
      if (it != collateral_blocked.end ())
        {
          debug.rdbuf () &&debug << "collateral_unblocking: " << it->id
                                 << " for port: " << it->port << " " << std::endl;
          coroutines.splice (coroutines.end (), collateral_blocked, it);
          coroutines.back ().port = nullptr;
          self->finished = true;
          self->yield_to (coroutines.back ());
        }
    }
  while (it != collateral_blocked.end ());

  if (collateral_blocked.end () == std::find_if (collateral_blocked.begin (),
                                                 collateral_blocked.end (),
                                                 predicate))
    {
      debug.rdbuf () &&debug << "everything unblocked!!!" << std::endl;
      unblocked.clear ();
    }
}
void pump::block (dzn::runtime &runtime, dzn::component *component, void *port)
{
  auto self = find_self (coroutines);
  runtime.blocked (component) = self->id;
  runtime.handling (component) = 0;
  runtime.flush (component, self->id);
  if (runtime.skip_block (component, port))
    {
      runtime.reset_skip_block (component);
      return;
    }
  self->port = port;
  debug.rdbuf () &&debug << "[" << self->id << "] block on "
                         << port << std::endl;

  bool collateral_skip = collateral_release_skip_block (component);
  if (!collateral_skip)
    {
      auto it = std::find_if
        (collateral_blocked.begin (),
         collateral_blocked.end (),
         [this] (const dzn::coroutine & coroutine)
         {
           return std::find (unblocked.begin (), unblocked.end (),
                             coroutine.port) != unblocked.end ();
         });
      if (it != collateral_blocked.end ())
        {
          debug.rdbuf () &&debug << "[" << it->id << "]" << " move from "
                                 << it->port << " to " << port << std::endl;
          it->port = port;
        }
      create_context ();
    }

  assert (coroutines.back ().port == nullptr);
  self->yield_to (coroutines.back ());
  debug.rdbuf () &&debug << "[" << self->id << "] entered context" << std::endl;
  if (debug.rdbuf ())
    {
      debug << "routines: ";
      for (auto &coroutine : coroutines)
        debug << coroutine.id << " ";
      debug << std::endl;
    }
  remove_finished_coroutines (coroutines);
  runtime.reset_skip_block (component);
  runtime.blocked (component) = 0;
}
bool pump::collateral_release_skip_block (dzn::component *component)
{
  bool have_collateral = false;
  collateral_blocked.reverse ();
  auto it = collateral_blocked.begin ();
  while (it != collateral_blocked.end ())
    {
      auto self = it++;
      if (self->component == component
          && std::find_if (unblocked.begin (), unblocked.end (),
                           [&] (void *port)
                           {return self->port == port;}) != unblocked.end ())
        {
          debug.rdbuf () &&debug << "[" << self->id << "]" << " relay skip "
                                 << self->port << std::endl;
          have_collateral = true;
          self->component = nullptr;
          self->port = nullptr;
          coroutines.splice (coroutines.end (), collateral_blocked, self);
        }
    }
  collateral_blocked.reverse ();
  return have_collateral;
}
void pump::release (dzn::runtime &runtime, dzn::component *component, void *port)
{
  runtime.set_skip_block (component, port);

  auto self = find_self (coroutines);
  debug.rdbuf () &&debug << "[" << self->id << "] release of "
                         << port << std::endl;

  auto blocked = find_blocked (coroutines, port);
  if (blocked == coroutines.end ())
    {
      debug.rdbuf () &&debug << "[" << self->id << "] skip block" << std::endl;
      return;
    }

  debug.rdbuf () &&debug << "[" << blocked->id << "] unblock" << std::endl;

  switch_context.emplace_back ([blocked, this]
  {
    auto self = find_self (this->coroutines);
    debug.rdbuf () &&debug << "setting unblocked to port "
                           << blocked->port << std::endl;
    this->unblocked.push_back (blocked->port);
    blocked->component = nullptr;
    blocked->port = nullptr;

    debug.rdbuf () &&debug << "[" << self->id << "] switch from" << std::endl;
    debug.rdbuf () &&debug << "[" << blocked->id << "] to" << std::endl;

    self->finished = true;
    self->yield_to (*blocked);
    assert (!"we must never return here!!!");
  });
}
void pump::operator () (const std::function<void ()> &event)
{
  assert (event);
  std::lock_guard<std::mutex> lock (mutex);
  queue.push (event);
  condition.notify_one ();
}
void pump::operator () (std::function<void ()> &&event)
{
  assert (event);
  std::lock_guard<std::mutex> lock (mutex);
  queue.push (std::move (event));
  condition.notify_one ();
}
void pump::defer (std::function<bool ()> &&predicate,
                  std::function<void (size_t)> &&event)
{
  deferred.emplace_back (std::move (predicate), std::move (event));
}
void pump::prune_deferred ()
{
  deferred.erase (std::remove_if (deferred.begin (), deferred.end (),
                                  [] (const typename decltype (deferred)::value_type & e)
                                  {return !e.first ();}),
                  deferred.end ());
}
void pump::handle (size_t identifier, size_t milliseconds,
                   const std::function<void ()> &event)
{
  assert (event);
  assert (std::find_if (timers.begin (), timers.end (),
                        [identifier] (const std::pair<deadline, std::function<void ()>> &pair)
                        { return pair.first.id == identifier; }) == timers.end ());
  timers.emplace (deadline (identifier, milliseconds), event);
}
void pump::remove (size_t identifier)
{
  auto it = std::find_if (timers.begin (), timers.end (),
                          [identifier] (const typename decltype (timers)::value_type & pair)
                          { return pair.first.id == identifier; });
  if (it != timers.end ()) timers.erase (it);
}
}
//version: 2.18.0
