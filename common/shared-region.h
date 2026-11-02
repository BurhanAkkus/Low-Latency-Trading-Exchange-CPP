#pragma once
#include "macros.h"
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <new>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace Common{
    // A named POSIX shared memory region (/dev/shm/<name>) holding one T, shared by two processes
    // on one machine. The creator (the exchange) makes it, zeroes it, constructs T in it and marks
    // it ready. The other process (the benchmark) attaches by name and waits until it's ready.
    // T has to work from both processes: no pointers inside it, its atomics lock-free.
    // Both mappings are populated up front: neither process takes a page fault on it later.
    template<typename T>
    class SharedRegion final{
        // In front of T, checked by the attaching process.
        struct Header{
            uint64_t magic_ = 0;
            // sizeof(T) in the creator: both processes were built with the same layout.
            uint64_t size_ = 0;
            // A region left behind by a creator that crashed still says ready: its creator is gone.
            pid_t creator_ = 0;
            std::atomic<uint32_t> ready_{0};
        };
        static_assert(std::atomic<uint32_t>::is_always_lock_free, "The ready flag must work across processes.");
        static constexpr uint64_t MAGIC = 0x314e474552534853; // "SHSREGN1"
        // T after the header, at T's alignment.
        static constexpr size_t OFFSET = (sizeof(Header) + alignof(T) - 1) / alignof(T) * alignof(T);
        static constexpr size_t BYTES = OFFSET + sizeof(T);

        std::string name_;
        void* base_ = nullptr;
        bool creator_ = false;

        auto header() noexcept { return static_cast<Header*>(base_); }
        auto map(int fd) -> void {
            base_ = mmap(nullptr, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
            close(fd);
            ASSERT(base_ != MAP_FAILED, "mmap of " + name_ + " failed: " + std::string(std::strerror(errno)));
        }
        public:
        enum class Open{CREATE, ATTACH};

        // CREATE: a new region, one left behind under the same name is removed first.
        // ATTACH: waits up to timeout for the creator to make the region and mark it ready.
        SharedRegion(const std::string& name, Open open,
            std::chrono::milliseconds timeout = std::chrono::seconds(60)) : name_(name), creator_(open == Open::CREATE) {
            if (creator_) {
                shm_unlink(name_.c_str());
                const int fd = shm_open(name_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
                ASSERT(fd >= 0, "shm_open of " + name_ + " failed: " + std::string(std::strerror(errno)));
                ASSERT(ftruncate(fd, BYTES) == 0, "ftruncate of " + name_ + " failed: " + std::string(std::strerror(errno)));
                map(fd);
                // Every page written once, so they're allocated now and not on first use.
                memset(base_, 0, BYTES);
                auto h = new (base_) Header;
                h->magic_ = MAGIC;
                h->size_ = sizeof(T);
                h->creator_ = getpid();
                new (get()) T;
                // T is constructed before an attaching process can see the flag.
                h->ready_.store(1, std::memory_order_release);
                return;
            }
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            auto wait = [&](const char* what) {
                if (std::chrono::steady_clock::now() > deadline) FATAL(name_ + ": " + what);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            };
            while (true) {
                const int fd = shm_open(name_.c_str(), O_RDWR, 0);
                if (fd < 0) { wait("no such region, is the exchange running?"); continue; }
                struct stat st{};
                // Not sized yet: the creator is between shm_open and ftruncate.
                if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < BYTES) { close(fd); wait("region too small"); continue; }
                map(fd);
                auto h = header();
                // A creator that crashed left it behind: wait for the next one, which replaces it.
                if (h->ready_.load(std::memory_order_acquire) && kill(h->creator_, 0) == 0) break;
                munmap(base_, BYTES);
                base_ = nullptr;
                wait("not ready");
            }
            ASSERT(header()->magic_ == MAGIC && header()->size_ == sizeof(T),
                name_ + " has a different layout, rebuild both processes.");
        }
        ~SharedRegion() {
            if (base_) munmap(base_, BYTES);
            if (creator_) shm_unlink(name_.c_str());
        }

        // Delete defaults.
        SharedRegion() = delete; // Default ctor
        SharedRegion(const SharedRegion&) = delete; // Copy ctor
        SharedRegion& operator=(const SharedRegion&) = delete; // Copy assignment
        SharedRegion(const SharedRegion&&) = delete; // Move ctor
        SharedRegion& operator=(const SharedRegion&&) = delete; // Move assignment

        auto get() noexcept -> T* { return reinterpret_cast<T*>(static_cast<char*>(base_) + OFFSET); }
        auto operator->() noexcept -> T* { return get(); }
    };
}
