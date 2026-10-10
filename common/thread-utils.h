#pragma once
#include <iostream>
#include <atomic>
#include <thread>
#include <string>
#include <tuple>
#include <unistd.h>
#include <sys/syscall.h>

namespace Common{
  // set core affinity for current thread.
  inline auto setThreadCore(int core_id) noexcept {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset); // clear cpu_set_t
    CPU_SET(core_id, &cpuset); // enable entry for core_id
    return (pthread_setaffinity_np(pthread_self(), sizeof
      (cpu_set_t), &cpuset) == 0);
  }

  // creates a thread, tries to assign core_id to its affinity.
  // passes func to run in created thread.
  // func and args are copied (or moved) into the thread, like std::thread does,
  // so temporaries are safe to pass. Use std::ref to pass something by reference.
  template<typename T, typename... A>
  inline auto createAndStartThread(int core_id, const
    std::string &name, T&& func, A &&... args) noexcept {
    std::atomic<bool> running(false), failed(false);
    // running/failed are captured by reference: the thread only touches them
    // before this function returns (we wait on them below).
    auto thread_body = [&running, &failed, core_id, name,
                        f = std::forward<T>(func),
                        a = std::make_tuple(std::forward<A>(args)...)]() mutable {
      if (core_id >= 0 && !setThreadCore(core_id)) {
        std::cerr << "Failed to set core affinity for " <<
          name << " " << pthread_self() << " to " << core_id
            << std::endl;
        failed = true;
        return;
      }
      std::cout << "Set core affinity for " << name << " " <<
        pthread_self() << " to " << core_id << std::endl;
      running = true;
      std::apply(std::move(f), std::move(a));
    };
    auto t = new std::thread(std::move(thread_body));
    while (!running && !failed) {
      using namespace std::literals::chrono_literals;
      std::this_thread::sleep_for(1s);
    }
    if (failed) {
      t->join();
      delete t;
      t = nullptr;
    }
    return t;
  }

}