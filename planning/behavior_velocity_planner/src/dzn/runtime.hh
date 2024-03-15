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
template <typename T>
std::string to_string (T t);
inline std::string
to_string (bool b)
{
  return b ? "true" : "false";
}

inline std::string
to_string (int i)
{
  return std::to_string (i);
}

inline void
to_void (std::string const &)
{
}

inline int
to_int (std::string s)
{
  return std::stoi (s);
}

inline bool
to_bool (std::string s)
{
  return s == "true";
}

extern std::ostream debug;

inline std::string
component_to_string (dzn::component *c)
{
  return c ? reinterpret_cast<component_meta *> (c)->dzn_meta.name : "<external>";
}

void trace_qin (std::ostream &, port::meta const &, char const *);
void trace_qout (std::ostream &, port::meta const &, char const *);

void trace_in (std::ostream &, port::meta const &, char const *);
void trace_out (std::ostream &, port::meta const &, char const *);

inline void
apply (dzn::meta const *m, const std::function<void (dzn::meta const *)> &f)
{
  f (m);
  for (auto c : m->children)
    {
      apply (c, f);
    }
}

inline void
check_bindings (dzn::meta const *c)
{
  apply (c, [] (dzn::meta const * m)
  {
    std::for_each (m->ports_connected.begin (), m->ports_connected.end (), [] (std::function<void ()> const &p) {p ();});
  });
}

inline void
check_bindings (dzn::component &c)
{
  check_bindings (&reinterpret_cast<component_meta const *> (&c)->dzn_meta);
}

template <typename Foreign>
inline void
check_bindings (Foreign const& f)
{
  check_bindings (&f.dzn_meta);
}

inline void
dump_tree (std::ostream &os, dzn::meta const *c)
{
  apply (c, [&] (dzn::meta const * m)
  {
    os << path (m) << ":" << m->type << std::endl;
  });
}

inline void
dump_tree (dzn::component const &c)
{
  dump_tree (std::clog, &reinterpret_cast<component_meta const *> (&c)->dzn_meta);
}

// implemented conditionally in pump.cc
void collateral_block (locator const &, dzn::component *);
bool port_blocked_p (locator const &, void *);
void port_block (locator const &, dzn::component *, void *);
void port_release (locator const &, dzn::component *, void *);
size_t coroutine_id (locator const &);
void defer (locator const &, std::function<bool ()> &&, std::function<void (size_t)> &&);
void prune_deferred (locator const &);

struct runtime
{
  runtime (runtime const &) = delete;
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
  bool queue_p (dzn::component *, dzn::component *);
  void enqueue (dzn::component *, dzn::component *, const std::function<void ()> &, size_t);
  runtime ();
};

template <typename C, typename P, typename E>
void defer (C *component, P &&predicate, E const &event)
{
  defer (component->dzn_locator, std::function<bool ()> (predicate),
         std::function<void (size_t)> ([ = ] (size_t coroutine_id)
         {
           component->dzn_runtime.handling (component) = coroutine_id;
           event ();
           component->dzn_runtime.flush (component);
           component->dzn_runtime.handling (component) = 0;
         }));
}

//https://cp-algorithms.com/string/string-hashing.html
inline std::uint32_t
hash (char const* l, std::uint32_t h)
{
  // numeric base for beginning of [0-9a-zA-Z] - 1, i.e. '0' = 48 - 1
  constexpr std::uint32_t b = 47;
  // smallest prime encompassing [0-9a-zA-Z] numerically
  constexpr std::uint32_t p = 79;
  std::string n = std::to_string (h);
  char const* s = n.c_str ();
  std::uint32_t pow = 1;
  h = 0;
  while (*s)
    {
      pow *= p;
      h = h + (*s++ - b) * pow;
    }
  while (*l)
    {
      pow *= p;
      h = h + (*l++ - b) * pow;
    }
  return h;
}

namespace in
{
template <typename Signature>
struct event;

template <typename R, typename...Args>
struct event<R (Args...)>
{
  std::function <void ()> dzn_out_binding;
  bool dzn_strict_p;
  R reply;
  void* port;
  std::function <void (char const*)> port_update;
  dzn::port::meta* dzn_port_meta;
  dzn::component* component;
  std::function <void ()> write_state;
  dzn::meta* dzn_meta;
  dzn::locator const* dzn_locator;
  dzn::runtime* dzn_runtime;
  std::ostream* os;
  char const* name;

  std::function<R (Args...)> f;
  event ()
    : dzn_strict_p (false)
    , reply ()
    , port ()
    , port_update ([] (char const*){})
    , dzn_port_meta ()
    , component ()
    , write_state ([]{})
    , dzn_meta ()
    , dzn_locator ()
    , dzn_runtime ()
    , os ()
    , name ()
  {}
  event (event const&) = default;
  event (event&&) = default;
  template <typename Component, typename Interface>
  void set (Component* c, Interface* i, char const* n)
  {
    this->dzn_strict_p = true;
    this->port = i;
    this->port_update = [i,c] (char const* s)
    {
      i->dzn_label = s;
      i->dzn_update_state (c->dzn_locator);
    };
    this->dzn_port_meta = &i->dzn_meta;
    this->component = c;
    this->write_state = []{};
    this->dzn_meta = &c->dzn_meta;
    this->dzn_locator = &c->dzn_locator;
    this->dzn_runtime = &c->dzn_runtime;
    this->os = &dzn_locator->get<std::ostream> ();
    this->name = n;
  }
  operator bool () const
  {
    return static_cast<bool> (this->f);
  }
  event& operator = (event const& that)
  {
    assert (that.port);
    assert (that.port_update);
    assert (that.dzn_port_meta);
    assert (that.component);
    assert (that.write_state);
    assert (that.dzn_meta);
    assert (that.dzn_locator);
    assert (that.dzn_runtime);

    std::function<R (Args...)> g (that.f);
    this->f = [g,this] (Args...args)
    {
      struct RAII
      {
        event* that;
        std::string reply_string;
        RAII (event* that)
          : that (that)
        {
          that->port_update (that->name);
        }
        R&& operator () (R&& r)
        {
          that->reply = r;
          return std::forward<R> (r);
        }
        ~RAII ()
        {
          this->reply_string = ::dzn::to_string (that->reply);
          that->port_update (this->reply_string.c_str ());
        }
      } raii (this);
      return raii (g (args...));
    };
    return *this;
  }
  template <typename Lambda>
  event& operator = (Lambda const& l)
  {
    return operator = (std::function<R (Args...)> (l));
  }
  event& operator = (std::function<R (Args...)>const & f)
  {
    this->f = [f,this] (Args ... args) -> decltype (f (args...))
    {
      if (!this->dzn_strict_p) return f (args...);

      assert (this->port);
      assert (this->port_update);
      assert (this->dzn_port_meta);
      assert (this->component);
      assert (this->write_state);
      assert (this->dzn_meta);
      assert (this->dzn_locator);
      assert (this->dzn_runtime);

      if (this->dzn_runtime->handling (this->dzn_port_meta->provide.component)
          || port_blocked_p (*this->dzn_locator, this->port))
        collateral_block (*this->dzn_locator, this->dzn_port_meta->provide.component);
      this->dzn_runtime->reset_skip_block (this->component);
      trace_in (*this->os, *this->dzn_port_meta, this->name);
      this->port_update (this->name);
      this->write_state ();
      this->dzn_runtime->handling (this->component) = coroutine_id (*this->dzn_locator);

      this->reply = f (args...);

      // possibly overwrites reply
      this->dzn_runtime->flush (this->dzn_port_meta->provide.component,
                                coroutine_id (*this->dzn_locator));

      std::string reply_string = ::dzn::to_string (this->reply);
      trace_out (*this->os, *this->dzn_port_meta, reply_string.c_str ());
      this->port_update (reply_string.c_str ());
      this->write_state ();
      prune_deferred (*this->dzn_locator);
      this->dzn_runtime->handling (this->component) = 0;

      if (this->dzn_out_binding) this->dzn_out_binding ();

      return this->reply;
    };
    return *this;
  }
  R operator () (Args...args)
  {
    return this->f (args...);
  }
};

template <typename...Args>
struct event<void (Args...)>
{
  std::function <void ()> dzn_out_binding;
  bool dzn_strict_p;
  void* port;
  std::function <void (char const*)> port_update;
  dzn::port::meta* dzn_port_meta;
  dzn::component* component;
  std::function <void ()> write_state;
  dzn::meta* dzn_meta;
  dzn::locator const* dzn_locator;
  dzn::runtime* dzn_runtime;
  std::ostream* os;
  char const* name;

  std::function<void (Args...)> f;
  event ()
    : dzn_strict_p (false)
    , port ()
    , port_update ([] (char const*){})
    , dzn_port_meta ()
    , component ()
    , write_state ([]{})
    , dzn_meta ()
    , dzn_locator ()
    , dzn_runtime ()
    , os ()
    , name ()
  {}
  event (event const&) = default;
  event (event&&) = default;
  template <typename Component, typename Interface>
  void set (Component* c, Interface* i, char const* n)
  {
    this->dzn_strict_p = true;
    this->port = i;
    this->port_update = [i,c] (char const* s)
    {
      i->dzn_label = s;
      i->dzn_update_state (c->dzn_locator);
    };
    this->dzn_port_meta = &i->dzn_meta;
    this->component = c;
    this->write_state = []{};
    this->dzn_meta = &c->dzn_meta;
    this->dzn_locator = &c->dzn_locator;
    this->dzn_runtime = &c->dzn_runtime;
    this->os = &dzn_locator->get<std::ostream> ();
    this->name = n;
  }
  operator bool () const
  {
    return static_cast<bool> (this->f);
  }
  event& operator = (event const& that)
  {
    assert (that.port);
    assert (that.port_update);
    assert (that.dzn_port_meta);
    assert (that.component);
    assert (that.write_state);
    assert (that.dzn_meta);
    assert (that.dzn_locator);
    assert (that.dzn_runtime);

    std::function<void (Args...)> g (that.f);
    this->f = [g,this] (Args...args)
    {
      struct RAII
      {
        event* that;
        char const* reply_string;
        RAII (event* that)
          : that (that)
          , reply_string ("return")
        {
          that->port_update (that->name);
        }
        ~RAII ()
        {
          that->port_update (reply_string);
        }
      } raii (this);
      return g (args...);
    };
    return *this;
  }
  template <typename Lambda>
  event& operator = (Lambda const& l)
  {
    return operator = (std::function<void (Args...)> (l));
  }
  event& operator = (std::function<void (Args...)> const& f)
  {
    this->f = [f,this] (Args... args) -> decltype (f (args...))
    {
      if (!dzn_strict_p) return f (args...);

      assert (this->port);
      assert (this->port_update);
      assert (this->dzn_port_meta);
      assert (this->component);
      assert (this->write_state);
      assert (this->dzn_meta);
      assert (this->dzn_locator);
      assert (this->dzn_runtime);

      if (this->dzn_runtime->handling (this->dzn_port_meta->provide.component)
          || port_blocked_p (*this->dzn_locator, this->port))
        collateral_block (*this->dzn_locator, this->dzn_port_meta->provide.component);
      this->dzn_runtime->reset_skip_block (this->component);
      trace_in (*this->os, *this->dzn_port_meta, this->name);
      this->port_update (this->name);
      this->write_state ();
      this->dzn_runtime->handling (this->component) = coroutine_id (*this->dzn_locator);

      f (args...);

      this->dzn_runtime->flush (this->dzn_port_meta->provide.component,
                                coroutine_id (*this->dzn_locator));
      trace_out (*this->os, *this->dzn_port_meta, "return");
      this->port_update ("return");
      this->write_state ();
      prune_deferred (*this->dzn_locator);
      this->dzn_runtime->handling (this->component) = 0;
      if (this->dzn_out_binding) this->dzn_out_binding ();
    };
    return *this;
  }
  void operator () (Args...args)
  {
    this->f (args...);
  }
};

}
namespace out
{
template <typename Signature>
struct event;

template <typename...Args>
struct event<void (Args...)>
{
  bool dzn_strict_p;
  void* port;
  std::function <void (char const*)> port_update;
  dzn::port::meta* dzn_port_meta;
  dzn::component* component;
  std::function <void ()> write_state;
  dzn::meta* dzn_meta;
  dzn::locator const* dzn_locator;
  dzn::runtime* dzn_runtime;
  std::ostream* os;
  char const* name;
  std::function<void (Args...)> f;

  event ()
    : dzn_strict_p (false)
    , port ()
    , port_update ([] (char const*){})
    , dzn_port_meta ()
    , component ()
    , write_state ([]{})
    , dzn_meta ()
    , dzn_locator ()
    , dzn_runtime ()
    , os ()
    , name ()
  {}
  event (event const&) = default;
  event (event&&) = default;
  template <typename Component, typename Interface>
  void set (Component* c, Interface* i, char const* n)
  {
    this->dzn_strict_p = true;
    this->port = i;
    this->port_update = [this,i,c] (char const* s)
    {
      i->dzn_label = s;
      i->dzn_update_state (c->dzn_locator);
    };
    this->dzn_port_meta = &i->dzn_meta;
    this->component = c;
    this->write_state = []{};
    this->dzn_meta = &c->dzn_meta;
    this->dzn_locator = &c->dzn_locator;
    this->dzn_runtime = &c->dzn_runtime;
    this->os = &dzn_locator->get<std::ostream> ();
    this->name = n;
  }
  operator bool () const
  {
    return static_cast<bool> (this->f);
  }
  event& operator = (event const& that)
  {
    assert (that.port);
    assert (that.port_update);
    assert (that.dzn_port_meta);
    assert (that.component);
    assert (that.write_state);
    assert (that.dzn_meta);
    assert (that.dzn_locator);
    assert (that.dzn_runtime);

    std::function<void (Args...)> g (that.f);
    this->f = [g,this] (Args...args)
    {
      this->port_update (this->name);
      return g (args...);
    };
    return *this;
  }
  template <typename Lambda>
  event& operator = (Lambda const& l)
  {
    return operator = (std::function<void (Args...)> (l));
  }
  event& operator = (std::function<void (Args...)> const& f)
  {
    this->f = [f,this] (Args...args)
    {
      if (!this->dzn_strict_p) return f (args...);

      assert (this->port);
      assert (this->port_update);
      assert (this->dzn_port_meta);
      assert (this->component);
      assert (this->write_state);
      assert (this->dzn_meta);
      assert (this->dzn_locator);
      assert (this->dzn_runtime);

      trace_qin (*this->os, *this->dzn_port_meta, this->name);
      this->port_update (this->name);
      this->write_state ();

      if (!this->dzn_port_meta->require.component) f (args...);
      else
        {
          bool receive = this->component == this->dzn_port_meta->provide.component;
          this->dzn_runtime->enqueue
            (receive ? this->component : this->dzn_port_meta->provide.component,
             receive ? this->dzn_port_meta->require.component : this->component,
             [f,this,args...]
             {
               trace_qout (*this->os, *this->dzn_port_meta, this->name);
               this->write_state ();
               f (args...);
             }, coroutine_id (*this->dzn_locator));
        }
      prune_deferred (*this->dzn_locator);
    };
    return *this;
  }
  void operator () (Args...args) const
  {
    return this->f (args...);
  }
};
}
}
#endif //DZN_RUNTIME_HH
//version: 2.18.1
