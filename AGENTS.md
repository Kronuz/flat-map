# Working on flat-map

Read the [cpp-libraries rules](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md) before changing a shared API. Keep one implementation, C++20, and no external dependencies. Preserve the documented type constraints, builder duplicate order, ownership and exception guarantees. Add actual regressions to `test/test.cc`; its CHECK statements remain active in Release builds.

Follow host load coordination where installed. Compare complete workloads and allocation peaks before claiming performance changes. Keep core container tests independent of consumers. Update README, tests, example and catalog alongside API changes. Preserve license notices and contribution provenance.

Benchmarks (`bench/bench.cc`, `bench/crossover.cc`) build with `-DFLAT_MAP_BUILD_BENCH=ON`, off by default; abseil and boost are found only by those targets.

Search and insert strategy: `flat_map_detail::search_for` and `shift_with_memmove` (top of `flat_map.hh`) hold the policy, and `test/strategy.cc` pins it and every search kernel against `std::lower_bound`. Change a constant only from `bench/crossover.cc` runs on arm64 and x86 (clang and gcc) and an A/B of `bench/bench.cc` against the previous header, on a quiet machine, with both the repeating and the random query column. Check the compiled loop on x86: clang turns a branchless select back into a branch unless the compare is marked unpredictable. `bytewise_shift` gates the `memmove`; widening it needs the standard's implicit-lifetime rules, a test in `test/strategy.cc` that fails when it is forced on, and the trait values on libstdc++ and libc++.
