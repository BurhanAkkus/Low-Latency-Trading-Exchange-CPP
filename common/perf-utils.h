#pragma once
#include <cstdint>
#include "time-utils.h"

namespace Common{
    // Read the TSC register: CPU clock cycles since reset.
    // constant_tsc/nonstop_tsc CPUs tick at a fixed rate regardless of frequency scaling,
    // so cycles / TSC GHz = nanoseconds.
    inline auto rdtsc() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

    // rdtsc doesn't wait for earlier instructions and later ones can run before it, so the out
    // of order core blurs a measured block's edges. lfence starts nothing until everything before
    // it has finished. Neither waits for stores to drain from the store buffer.
    // Start of a block: read after earlier code has finished, before the block starts.
    inline auto rdtscStart() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("lfence\n\trdtsc\n\tlfence" : "=a" (lo), "=d" (hi) :: "memory");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }
    // End of a block: rdtscp waits for the block's instructions, lfence keeps later code out.
    inline auto rdtscEnd() noexcept {
        unsigned int lo, hi;
        __asm__ __volatile__ ("rdtscp\n\tlfence" : "=a" (lo), "=d" (hi) :: "rcx", "memory");
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

    // Trace keys, see TRACE. A request: client id, client order id and whether it is a CANCEL (the
    // NEW and the CANCEL of one order share the order id). A market update: ticker, market order
    // id and whether it is a CANCEL (else an ADD).
    constexpr auto requestKey(uint64_t client_id, uint64_t client_order_id, bool cancel) noexcept {
        return (client_id << 32) | (client_order_id << 1) | cancel;
    }
    constexpr auto updateKey(uint64_t ticker_id, uint64_t market_order_id, bool cancel) noexcept {
        return (ticker_id << 40) | (market_order_id << 1) | cancel;
    }
}

// LOGGER is a Logger (not a pointer).
// Log lines are "<TAG> <value>\n" and parsed by scripts/perf-analysis.py: a code block's cycles,
// or for TTT tags (the hop names in perf-analysis.py) the epoch nanos at that hop.
// The value is pushed as an integer, the logger thread formats it.
// The '\n' is needed: the logger writes no separator of its own, without it a line runs into the
// next one.

// Start measuring a code block. Creates a variable called TAG in the local scope.
#define START_MEASURE(TAG) const auto TAG = Common::rdtscStart()

// End measuring a code block started with START_MEASURE(TAG), logs elapsed cycles.
// Keep TAG at most 7 characters: "<TAG> " then fits in 1 log element (8 chars), 3 per measurement
// with the value and the '\n'.
#define END_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto end = Common::rdtscEnd();                                                    \
        LOGGER.log(#TAG " %\n", (end - TAG));                                                   \
    } while(false)

// Per-request tracing: every stage of a request's path logs "<TAG> <key> <tsc>\n" (5 log
// elements, ~40 ns), scripts/trace-analysis.py joins the stages by key. The TSC is one clock for
// all cores and processes, so stages logged by different threads and by the benchmark's clients
// compare directly. Comment a TRACE out to remove that stage.
#define TRACE(TAG, KEY, LOGGER)                                                                 \
    do {                                                                                        \
        LOGGER.log(#TAG " % %\n", static_cast<uint64_t>(KEY), Common::rdtsc());                 \
    } while(false)

// TRACE with a TSC read earlier: for a stage start that is only worth logging once it's known
// how the stage ended (a syscall that did something).
#define TRACE_AT(TAG, KEY, TSC, LOGGER)                                                         \
    do {                                                                                        \
        LOGGER.log(#TAG " % %\n", static_cast<uint64_t>(KEY), static_cast<uint64_t>(TSC));      \
    } while(false)

// Time-to-tick: log the current nanosecond timestamp at a hop on the order's path.
// The difference between two hops' timestamps is the latency between them.
#define TTT_MEASURE(TAG, LOGGER)                                                                \
    do {                                                                                        \
        const auto TAG = Common::getCurrentNanos();                                             \
        LOGGER.log(#TAG " %\n", TAG);                                                           \
    } while(false)
