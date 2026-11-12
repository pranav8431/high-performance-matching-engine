#pragma once

// Fixed-resolution latency histogram for per-operation timings.
//
// 1 ns buckets up to `max_ns`, plus an overflow bucket. Recording is a single
// array increment, so it adds almost nothing on top of the clock read itself.
// Percentiles are exact to 1 ns within range.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace lob {

class LatencyHistogram {
public:
    explicit LatencyHistogram(std::uint64_t max_ns = 100'000)
        : buckets_(max_ns + 1, 0), max_ns_(max_ns) {}

    void record(std::uint64_t ns) noexcept {
        ++count_;
        sum_ += ns;
        if (ns < min_) min_ = ns;
        if (ns > max_) max_ = ns;
        if (ns > max_ns_) ++overflow_; else ++buckets_[ns];
    }

    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t min() const noexcept { return count_ ? min_ : 0; }
    std::uint64_t max() const noexcept { return max_; }
    double        mean() const noexcept { return count_ ? static_cast<double>(sum_) / static_cast<double>(count_) : 0.0; }

    // p in [0, 100]. Returns max_ns_ + 1 if the percentile falls in overflow.
    std::uint64_t percentile(double p) const noexcept {
        if (count_ == 0) return 0;
        const std::uint64_t target = static_cast<std::uint64_t>(p / 100.0 * static_cast<double>(count_) + 0.5);
        std::uint64_t seen = 0;
        for (std::uint64_t i = 0; i < buckets_.size(); ++i) {
            seen += buckets_[i];
            if (seen >= target && seen > 0) return i;
        }
        return max_ns_ + 1;
    }

    void merge(const LatencyHistogram& other) noexcept {
        for (std::size_t i = 0; i < buckets_.size() && i < other.buckets_.size(); ++i) buckets_[i] += other.buckets_[i];
        overflow_ += other.overflow_;
        count_ += other.count_;
        sum_ += other.sum_;
        min_ = std::min(min_, other.min_);
        max_ = std::max(max_, other.max_);
    }

    void print(const char* label, std::FILE* out = stdout) const {
        if (count_ == 0) {
            std::fprintf(out, "%-14s  (no samples)\n", label);
            return;
        }
        std::fprintf(out, "%-14s %10llu %8.1f %7llu %7llu %7llu %8llu %8llu %9llu\n",
                     label,
                     static_cast<unsigned long long>(count_), mean(),
                     static_cast<unsigned long long>(percentile(50)),
                     static_cast<unsigned long long>(percentile(90)),
                     static_cast<unsigned long long>(percentile(99)),
                     static_cast<unsigned long long>(percentile(99.9)),
                     static_cast<unsigned long long>(percentile(99.99)),
                     static_cast<unsigned long long>(max_));
    }

    static void print_header(std::FILE* out = stdout) {
        std::fprintf(out, "%-14s %10s %8s %7s %7s %7s %8s %8s %9s\n",
                     "operation", "samples", "mean", "p50", "p90", "p99", "p99.9", "p99.99", "max");
        std::fprintf(out, "%-14s %10s %8s %7s %7s %7s %8s %8s %9s\n",
                     "", "", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)", "(ns)");
    }

private:
    std::vector<std::uint64_t> buckets_;
    std::uint64_t max_ns_;
    std::uint64_t overflow_ = 0;
    std::uint64_t count_    = 0;
    std::uint64_t sum_      = 0;
    std::uint64_t min_      = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t max_      = 0;
};

}  // namespace lob
