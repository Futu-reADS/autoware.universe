// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2016, 2017, 2019, 2020, 2021, 2022, 2023 Janneke Nieuwenhuizen <janneke@gnu.org>
// Copyright © 2016 Rob Wieringa <rma.wieringa@gmail.com>
// Copyright © 2016 Henk Katerberg <hank@mudball.nl>
// Copyright © 2016, 2017, 2018, 2019, 2020, 2021, 2022, 2023 Rutger van Beusekom <rutger@dezyne.org>
//
// This file is part of dzn-runtime.
//
// All rights reserved.
//
//
// Commentary:
//
// Code:

#ifndef DZN_RUNTIME_HH
#define DZN_RUNTIME_HH

#include <dzn/config.hh>
#include <dzn/meta.hh>
#include <dzn/locator.hh>
#include <dzn/coroutine.hh>

#include <algorithm>
#include <cstddef>
#include <future>
#include <iostream>
#include <map>
#include <queue>
#include <tuple>
#include <vector>

// Set to 1 for experimental state tracing feature.
#ifndef DZN_STATE_TRACING
#define DZN_STATE_TRACING 0
#endif

namespace dzn
{
inline std::string to_string (bool b) {return b ? "true" : "false";}
inline std::string to_string (int i) {return std::to_string (i);}
inline void to_void (const std::string &) {}
inline int to_int (std::string s) {return std::stoi (s);}
inline bool to_bool (std::string s) {return s == "true";}

extern std::ostream debug;

inline std::string component_to_string (dzn::component *c)
{
  return c ? reinterpret_cast<component_meta *> (c)->dzn_meta.name : "<external>";
}

void trace_qin (std::ostream &, port::meta const &, const char *);
void trace_qout (std::ostream &, port::meta const &, const char *);

void trace (std::ostream &, port::meta const &, const char *);
void trace_out (std::ostream &, port::meta const &, const char *);

inline void apply (const dzn::meta *m, const std::function<void (const dzn::meta *)> &f)
{
  f (m);
  for (auto c : m->children)
    {
      apply (c, f);
    }
}

template <typename P, typename R>
void connect (P& provided, R& required)
{
  provided.out = required.out;
  required.in = provided.in;
  provided.dzn_meta.require = required.dzn_meta.require;
  required.dzn_meta.provide = provided.dzn_meta.provide;
  provided.dzn_peer = &required;
  required.dzn_peer = &provided;
  provided.dzn_share_p = required.dzn_share_p
    = provided.dzn_share_p && required.dzn_share_p;
}

inline void check_bindings (const dzn::meta *c)
{
  apply (c, [] (const dzn::meta * m)
  {
    std::for_each (m->ports_connected.begin (), m->ports_connected.end (), [] (const std::function<void ()> &p) {p ();});
  });
}

inline void check_bindings (dzn::component &c)
{
  check_bindings (&reinterpret_cast<component_meta const *> (&c)->dzn_meta);
}

inline void dump_tree (std::ostream &os, const dzn::meta *c)
{
  apply (c, [&] (const dzn::meta * m)
  {
    os << path (m) << ":" << m->type << std::endl;
  });
}

inline void dump_tree (dzn::component const &c)
{
  dump_tree (std::clog, &reinterpret_cast<component_meta const *> (&c)->dzn_meta);
}

// implemented conditionally in pump.cc
void collateral_block (const locator &, dzn::component *);
bool port_blocked_p (const locator &, void *);
void port_block (const locator &, dzn::component *, void *);
void port_release (const locator &, dzn::component *, void *);
size_t coroutine_id (const locator &);
void defer (const locator &, std::function<bool ()> &&, std::function<void (size_t)> &&);
void prune_deferred (const locator &);

struct runtime
{
  runtime (const runtime &) = delete;
  runtime (runtime &&) = delete;
  struct state
  {
    size_t handling;
    size_t blocked;
    void *skip;
    bool performs_flush;
    dzn::component *deferred;
    std::queue<std::function<void ()>> queue;
  };
  std::map<dzn::component *, state> states;
  bool skip_block (dzn::component *, void *);
  void set_skip_block (dzn::component *, void *);
  void reset_skip_block (dzn::component *);

  bool external (dzn::component *);
  size_t &handling (dzn::component *);
  size_t &blocked (dzn::component *);
  dzn::component *&deferred (dzn::component *);
  std::queue<std::function<void ()> > &queue (dzn::component *);
  bool &performs_flush (dzn::component *);
  template <typename T>
  void flush (T *t)
  {
    flush (t, coroutine_id (t->dzn_locator));
  }
  void flush (dzn::component *, size_t);
  bool async (dzn::component *, dzn::component *);
  void enqueue (dzn::component *, dzn::component *, const std::function<void ()> &, size_t);
  template <typename F, typename = typename std::enable_if<std::is_void<typename std::result_of<F ()>::type>::value>::type>
  void handle (dzn::component *component, F && f, size_t coroutine_id)
  {
    size_t &handle = handling (component);
    if (handle) throw std::logic_error ("component already handling an event");
    handle = coroutine_id;
    assert (handle != 0);
    f ();
  }
  template < typename F, typename = typename std::enable_if < !std::is_void<typename std::result_of<F ()>::type>::value >::type >
  inline auto handle (dzn::component *component, F && f, size_t coroutine_id) -> decltype (f ())
  {
    size_t &handle = handling (component);
    if (handle) throw std::logic_error ("component already handling an event");
    handle = coroutine_id;
    return f ();
  }
  runtime ();
};

template <typename P>
struct share_trace_wrapper
{
  const dzn::locator &locator;
  P &port;
  char const *event_name;
  std::string reply;
  std::ostream &os;
  bool qout;
  share_trace_wrapper (const dzn::locator &l, P &port, char const *event_name, bool qout = false)
    : locator (l)
    , port (port)
    , event_name (event_name)
    , reply ("return")
    , os (l.template get<typename std::ostream> ())
    , qout (qout)
  {
    if (!qout)
      {
        trace (os, port.dzn_meta, event_name);
        port.dzn_event (event_name);
        port.dzn_busy = true;
      }
    else trace_qout (os, port.dzn_meta, event_name);
  }
  template <typename E, typename = typename std::enable_if<std::is_void<typename std::result_of<E ()>::type>::value>::type>
  void operator () (E && event)
  {
    event ();
  }
  template < typename E, typename = typename std::enable_if < !std::is_void<typename std::result_of<E ()>::type>::value >::type >
  auto operator () (E && event) -> decltype (event ())
  {
    return handle_reply (event);
  }
  template <typename E>
  auto handle_reply (E &&event) -> decltype (event ())
  {
    auto value = event ();
    reply = to_string (value);
    return value;
  }
  ~share_trace_wrapper ()
  {
    if (!qout)
      {
        trace_out (os, port.dzn_meta, reply.c_str ());
        port.dzn_event (reply.c_str ());
        port.dzn_busy = false;
        port.dzn_update_state (locator);
      }
  }
};

template <typename C, typename P>
struct runtime_wrapper
{
  C *component;
  runtime_wrapper (C *component, P &port, bool qout = false)
    : component (component)
  {
    if (!qout)
      {
        if (component->dzn_runtime.handling (component)
            || port_blocked_p (component->dzn_locator, &port))
          {
            collateral_block (component->dzn_locator, component);
          }

        component->dzn_runtime.reset_skip_block (component);
      }
#if DZN_STATE_TRACING
    this->os << *component << std::endl;
#endif
  }
  template <typename E, typename = typename std::enable_if<std::is_void<typename std::result_of<E ()>::type>::value>::type>
  void operator () (E && event)
  {
    component->dzn_runtime.handle (component, event, coroutine_id (component->dzn_locator));
  }
  template < typename E, typename = typename std::enable_if < !std::is_void<typename std::result_of<E ()>::type>::value >::type >
  auto operator () (E && event) -> decltype (event ())
  {
    return component->dzn_runtime.handle (component, event, coroutine_id (component->dzn_locator));
  }
  ~runtime_wrapper ()
  {
#if DZN_STATE_TRACING
    this->os << *component << std::endl;
#endif
    prune_deferred (component->dzn_locator);
    component->dzn_runtime.handling (component) = 0;
  }
};

template <typename C, typename P>
struct wrapper: public runtime_wrapper<C, P>, public share_trace_wrapper<P>
{
  wrapper (C *component, P &port, char const *event_name, bool qout = false)
    : runtime_wrapper<C, P> (component, port, qout)
    , share_trace_wrapper<P> (component->dzn_locator, port, event_name, qout)
  {}
  template <typename E>
  auto operator () (E e) -> decltype (e ())
  {
    return runtime_wrapper<C, P>::operator () ([&] {return share_trace_wrapper<P>::operator () (e);});
  }
};

template <typename C, typename P, typename E>
auto wrap_in (C *component, P &port, const E &event, char const *name) -> decltype (event ())
{
  return wrapper<C, P> (component, port, name) (event);
}

template <typename C, typename P, typename E>
void wrap_out (C *component, P &port, E event, char const *name)
{
  auto &os = component->dzn_locator.template get<typename std::ostream> ();
  trace_qin (os, port.dzn_meta, name);
  port.dzn_event (name);
  if (!port.dzn_busy) port.dzn_update_state (component->dzn_locator);
  component->dzn_runtime.enqueue
    (port.dzn_meta.provide.component, component,
     [component, &port, &os, event, name]
     {
       trace_qout (os, port.dzn_meta, name);
       event ();
     }, coroutine_id (component->dzn_locator));
  component->dzn_update ();
  prune_deferred (component->dzn_locator);
}

template <typename C, typename P, typename E>
auto call_in (C *component, P &port, const char *name, const E &event) -> decltype (event ())
{
  return port.dzn_peer ? event () : share_trace_wrapper<P> (component->dzn_locator, port, name) (event);
}

template <typename C, typename P, typename E>
void call_out (C *component, P &port, const char *name, const E &event)
{
  if (port.dzn_peer) return  event ();
  auto &os = component->dzn_locator.template get<typename std::ostream> ();
  trace_qin (os, port.dzn_meta, name);
  port.dzn_event (name);
  return share_trace_wrapper<P> (component->dzn_locator, port, name, true) (event);
}

template <typename C, typename P, typename E>
void defer (C *component, P &&predicate, const E &event)
{
  defer (component->dzn_locator, std::function<bool ()> (predicate),
         std::function<void (size_t)> ([ = ] (size_t coroutine_id)
         {
           component->dzn_runtime.handling (component) = coroutine_id;
           event ();
           component->dzn_runtime.flush (component);
           component->dzn_runtime.handling (component) = 0;
           component->dzn_update ();
         }));
}
//https://cp-algorithms.com/string/string-hashing.html
inline std::uint32_t hash (const std::vector<char const *> &r, std::uint32_t h)
{
  // numeric base for beginning of [0-9a-zA-Z] - 1, i.e. '0' = 48 - 1
  constexpr std::uint32_t b = 47;
  // smallest prime encompassing [0-9a-zA-Z] numerically
  constexpr std::uint32_t p = 79;
  std::uint32_t pow = 1;
  h *= p;
  for (auto s : r)
    while (*s)
      {
        h = h + (*s++ - b) * pow;
        pow *= p;
      }
  return h;
}
}
#endif //DZN_RUNTIME_HH
//version: 2.18.0.rc6
