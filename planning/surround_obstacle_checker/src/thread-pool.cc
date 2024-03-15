// dzn-runtime -- Dezyne runtime library
//
// Copyright © 2020, 2023 Rutger van Beusekom <rutger@dezyne.org>
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

#include <cassert>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <vector>

namespace dzn
{
namespace thread
{
class task;
class pool
{
  class task;
  friend class task;
  std::mutex mut_;
  std::queue<task *> idle_tasks_;
  std::vector<std::unique_ptr<task> > tasks_;
public:
  pool () {}
  ~pool ()
  {
    if (idle_tasks_.size () != tasks_.size ()) std::abort ();
  }
  std::future<void> async (std::function<void ()> const &work)
  {
    std::unique_lock<std::mutex> lock (mut_);
    if (idle_tasks_.empty ())
      {
        tasks_.emplace_back (new task (*this));
        return tasks_.back ()->assign (work);
      }
    else
      {
        std::future<void> fut = idle_tasks_.front ()->assign (work);
        idle_tasks_.pop ();
        return fut;
      }
  }
  size_t size () const
  {
    return tasks_.size ();
  }
private:
  pool &operator = (pool const &);
  pool (pool const &);

  void idle (task *t)
  {
    std::unique_lock<std::mutex> lock (mut_);
    idle_tasks_.push (t);
  }
  class task
  {
    pool &pool_;
    bool running_;
    std::function<void ()> work_;
    std::promise<void> promise_;
    std::condition_variable con_;
    std::mutex mut_;
    std::thread thread_;
  public:
    task (pool &p)
      : pool_ (p)
      , running_ (true)
      , work_ ()
      , con_ ()
      , mut_ ()
      , thread_ (std::bind (&task::worker, self ()))
    {}
    ~task ()
    {
      std::unique_lock<std::mutex> lock (mut_);
      running_ = false;
      con_.notify_one ();
      lock.unlock ();
      thread_.join ();
    }
    std::future<void> assign (std::function<void ()> work)
    {
      std::unique_lock<std::mutex> lock (mut_);
      assert (!work_);
      work_.swap (work);
      promise_ = std::promise<void> ();
      con_.notify_one ();
      return promise_.get_future ();
    }
  private:
    task *self () { return this; }
    void worker ()
    {
      std::unique_lock<std::mutex> lock (mut_);
      do
        {
          while (running_ && !work_)
            con_.wait (lock);
          if (work_)
            {
              std::function<void ()> work;
              work_.swap (work);
              lock.unlock ();
              work ();
              lock.lock ();
              pool_.idle (this);
              promise_.set_value ();
            }
        }
      while (running_);
    }
    task (task const &);
    task &operator = (task const &);
  };
};
}

static thread::pool &
thread_pool ()
{
  static thread::pool tp;
  return tp;
}

std::future<void>
std_async (std::function<void ()> const &work)
{
  return thread_pool ().async (work);
}

size_t
thread_pool_size ()
{
  return thread_pool ().size ();
}
}
