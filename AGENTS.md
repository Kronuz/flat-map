# Working on flat-map

Read the [cpp-libraries rules](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md) before changing a shared API. Keep one implementation, C++20, and no external dependencies. Preserve the documented type constraints, builder duplicate order, ownership and exception guarantees. Add actual regressions to `test/test.cc`; its CHECK statements remain active in Release builds.

Follow host load coordination where installed. Compare complete workloads and allocation peaks before claiming performance changes. Keep core container tests independent of consumers. Update README, tests, example and catalog alongside API changes. Preserve license notices and contribution provenance.
