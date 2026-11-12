#pragma once

// Shared helpers for the benchmark executables.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#define LOB_HAVE_TSC 1
#endif

namespace lob::bench {

using Clock = std::chrono::steady_clock;

// Cycle counter for per-operation latency. On x86-64 this is rdtsc, which
// costs ~10 ns, versus clock_gettime which can cost >1 us on machines whose
// clocksource is hpet/acpi_pm. The TSC is calibrated against steady_clock at
// start-up. Requires constant_tsc/nonstop_tsc (any CPU from the last decade).
class TscTimer {
public:
    TscTimer() { calibrate(); }

    static inline std::uint64_t start() noexcept {
#ifdef LOB_HAVE_TSC
        _mm_lfence();  // do not let earlier instructions drift into the timed region
        return __rdtsc();
#else
        return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
    }

    static inline std::uint64_t stop() noexcept {
#ifdef LOB_HAVE_TSC
        unsigned aux;
        const std::uint64_t t = __rdtscp(&aux);  // waits for prior loads/stores to complete
        _mm_lfence();
        return t;
#else
        return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
#endif
    }

    std::uint64_t to_ns(std::uint64_t ticks) const noexcept {
        return static_cast<std::uint64_t>(static_cast<double>(ticks) * ns_per_tick_ + 0.5);
    }

    double ghz() const noexcept { return 1.0 / ns_per_tick_; }

    // Cost of an empty start()/stop() pair, in ns. Included in every sample.
    std::uint64_t overhead_ns() const noexcept { return overhead_ns_; }

    const char* source() const noexcept {
#ifdef LOB_HAVE_TSC
        return "rdtsc";
#else
        return "steady_clock";
#endif
    }

private:
    void calibrate() {
#ifdef LOB_HAVE_TSC
        const auto c0 = Clock::now();
        const std::uint64_t t0 = __rdtsc();
        while (std::chrono::duration<double>(Clock::now() - c0).count() < 0.2) {}
        const std::uint64_t t1 = __rdtsc();
        const auto c1 = Clock::now();
        const double ns = std::chrono::duration<double, std::nano>(c1 - c0).count();
        ns_per_tick_ = ns / static_cast<double>(t1 - t0);
#else
        ns_per_tick_ = 1.0;
#endif
        std::uint64_t best = ~std::uint64_t{0};
        for (int i = 0; i < 100'000; ++i) {
            const std::uint64_t a = start();
            const std::uint64_t b = stop();
            if (b - a < best) best = b - a;
        }
        overhead_ns_ = to_ns(best);
    }

    double        ns_per_tick_ = 1.0;
    std::uint64_t overhead_ns_ = 0;
};

inline double seconds_since(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

inline std::uint64_t ns_between(Clock::time_point a, Clock::time_point b) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

inline std::size_t arg_size(int argc, char** argv, int i, std::size_t fallback) {
    return argc > i ? static_cast<std::size_t>(std::strtoull(argv[i], nullptr, 10)) : fallback;
}

inline std::string with_commas(std::uint64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

inline void print_rate(const char* label, std::uint64_t ops, double seconds) {
    std::printf("  %-34s %14s ops  %9.4f s  %8.2f M ops/s  %7.1f ns/op\n", label,
                with_commas(ops).c_str(), seconds, static_cast<double>(ops) / seconds / 1e6,
                seconds * 1e9 / static_cast<double>(ops));
}

inline void print_build_info() {
#if defined(__clang__)
    std::printf("compiler: clang %d.%d.%d", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
    std::printf("compiler: gcc %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#endif
#if defined(__OPTIMIZE__)
    std::printf("  optimised build");
#else
    std::printf("  UNOPTIMISED BUILD - numbers are meaningless");
#endif
#if defined(__AVX2__)
    std::printf("  (-march=native or AVX2 enabled)");
#endif
    std::printf("\n");
}

}  // namespace lob::bench
