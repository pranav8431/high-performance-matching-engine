// Generate a synthetic event file for the replay tool.
//
//   gen_workload <out.txt> [events=1000000] [seed=42]
//
// Uses the same WorkloadGenerator as the benchmarks, so a replayed file
// reproduces the benchmark flow exactly.

#include <cstdio>
#include <cstdlib>
#include <string>

#include "lob/event_parser.hpp"
#include "lob/workload.hpp"

using namespace lob;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: gen_workload <out.txt> [events] [seed]\n");
        return 2;
    }
    const std::size_t   n    = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1'000'000;
    const std::uint64_t seed = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 42;

    std::FILE* out = std::fopen(argv[1], "w");
    if (!out) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }

    WorkloadConfig cfg;
    cfg.seed = seed;
    WorkloadGenerator gen(cfg);

    std::fprintf(out, "# %zu synthetic events, seed %llu, price range [%lld, %lld], start mid %lld\n", n,
                 (unsigned long long)seed, (long long)cfg.min_price, (long long)cfg.max_price, (long long)cfg.start_mid);
    std::string line;
    for (std::size_t i = 0; i < n; ++i) {
        EventParser::format(gen.next(), line);
        line += '\n';
        std::fputs(line.c_str(), out);
    }
    std::fclose(out);
    std::printf("wrote %zu events to %s\n", n, argv[1]);
    return 0;
}
