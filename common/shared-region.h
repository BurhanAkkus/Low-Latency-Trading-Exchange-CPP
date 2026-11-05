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
    // Where the regions are: hugetlbfs, so they're on 2 MB pages. benchmarks/prepare-machine.sh
    // reserves the pages and makes the directory, after every boot.
    inline constexpr const char* SHARED_REGION_DIR = "/dev/hugepages/exchange";
    constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024;

    // A named shared memory region (SHARED_REGION_DIR/<name>) holding one T, shared by two
    // processes on one machine. The creator (the exchange) makes it, zeroes it, constructs T in it
    // and marks it ready. The other process (the benchmark) attaches by name and waits until it's
    // ready. T has to work from both processes: no pointers inside it, its atomics lock-free.
    // Both mappings are populated up front: neither process takes a page fault on it later. On
    // 2 MB pages: the order server's 255 clients' rings are 16 pages instead of ~8000, their
    // address translations stay in the TLB.
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
        // Whole huge pages: hugetlbfs maps and unmaps nothing smaller.
        static constexpr size_t BYTES = (OFFSET + sizeof(T) + HUGE_PAGE_SIZE - 1) / HUGE_PAGE_SIZE * HUGE_PAGE_SIZE;

        std::string name_;
        std::string path_;
        void* base_ = nullptr;
        bool creator_ = false;

        auto header() noexcept { return static_cast<Header*>(base_); }
        auto map(int fd) -> void {
            base_ = mmap(nullptr, BYTES, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd, 0);
            close(fd);
            // ENOMEM: fewer huge pages free than the region needs.
            ASSERT(base_ != MAP_FAILED, "mmap of " + path_ + " failed: " + std::string(std::strerror(errno)) +
                ", are the huge pages reserved? sudo benchmarks/prepare-machine.sh");
        }
        public:
        enum class Open{CREATE, ATTACH};

        // CREATE: a new region, one left behind under the same name is removed first.
        // ATTACH: waits up to timeout for the creator to make the region and mark it ready.
        // name starts with a '/'.
        SharedRegion(const std::string& name, Open open,
            std::chrono::milliseconds timeout = std::chrono::seconds(60))
            : name_(name), path_(SHARED_REGION_DIR + name), creator_(open == Open::CREATE) {
            if (creator_) {
                unlink(path_.c_str());
                const int fd = ::open(path_.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
                ASSERT(fd >= 0, "open of " + path_ + " failed: " + std::string(std::strerror(errno)) +
                    ", run sudo benchmarks/prepare-machine.sh");
                ASSERT(ftruncate(fd, BYTES) == 0, "ftruncate of " + path_ + " failed: " + std::string(std::strerror(errno)));
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
                const int fd = ::open(path_.c_str(), O_RDWR);
                if (fd < 0) { wait("no such region, is the exchange running?"); continue; }
                struct stat st{};
                // Not sized yet: the creator is between open and ftruncate.
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
            if (creator_) unlink(path_.c_str());
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
