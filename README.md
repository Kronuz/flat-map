# flat-map

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/flat-map/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/flat-map/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)
![Header-only](https://img.shields.io/badge/header--only-yes-green.svg)

Header-only sorted-vector map for C++20: bulk construction, linear merges, and a branchless search for integer keys.
Entries are `std::pair<Key, T>` in one sorted `std::vector`, so a lookup is a
binary search over contiguous memory and iteration is a linear scan. Reach for
it when a map is built in bulk and read far more often than it changes.

## ✨ Highlights

- **One vector of pairs.** `kronuz::flat_map<Key, T, Compare>` keeps its
  entries sorted by key in a single `std::vector`.
- **Bulk construction.** `flat_map_builder` appends in any order and sorts once
  in `finish()`, which returns the input index of the first repeated key; sorted
  input skips the sort.
- **Linear merge.** `flat_map_merge(a, b, a_only, b_only, both)` walks two maps
  in one pass and calls one callback per key.
- **Branchless integer search.** Integer keys under `std::less` select each step
  with a conditional move; tables of 512 KiB or more also prefetch the next step.
- **Byte shifts where valid.** A middle insert moves a tail of 32 entries (and
  256 bytes) or more with one `memmove` when the entry type allows it.
- **Heterogeneous lookup.** With the default `std::less<>`, `find`, `at`,
  `contains`, `count`, `lower_bound`, and `erase` take any comparable key type.
- **Strong copy assignment.** A failed copy leaves the target unchanged.

## 🚀 Quick start

```cpp
#include "flat_map.hh"

#include <cstdio>
#include <string>

int main() {
	kronuz::flat_map<std::string, int> ages;
	kronuz::flat_map_builder<std::string, int> build(ages);
	build.push("carol", 41);
	build.push("alice", 33);
	build.push("bob", 27);
	if (build.finish())  // the input index of the first repeated key, if any
		return 1;

	ages.try_emplace("dave", 19);  // single inserts after finish()
	for (const auto& [name, age] : ages)
		std::printf("%s %d\n", name.c_str(), age);  // alice 33, bob 27, carol 41, dave 19
	std::printf("%d\n", ages.at("bob"));  // 27
}
```

[`examples/demo.cc`](examples/demo.cc) is a runnable example.

## 📦 Installation

Requires a C++20 compiler and CMake 3.24 or newer; no dependencies. CI builds it
on Linux (x86_64, arm64), macOS (arm64, x86_64), and FreeBSD, plus ASan and UBSan
builds; TSan is off because the container has no concurrent API.

```cmake
include(FetchContent)
FetchContent_Declare(flat_map
  GIT_REPOSITORY https://github.com/Kronuz/flat-map.git
  GIT_TAG        7ef6a916d54609748b301ca0fb37b2876915c7fc)  # or main for the tip
FetchContent_MakeAvailable(flat_map)

target_link_libraries(my_app PRIVATE flat_map::flat_map)
```

```cpp
#include "flat_map.hh"
```

## 🎯 When to use it

Use it for lookup tables and indexes that are loaded in bulk and then read, and
for sorted key sets that are merged against each other.

Not for:

- **Frequent middle inserts or erases.** Each one moves O(n) entries, and
  repeated positional erasure can be quadratic; `erase_if` compacts in one pass.
- **Stable references.** Every insert or erase invalidates iterators and
  references to entries, and `reserve` does not prevent the shift.
- **C++23 `std::flat_map` compatibility.** Keys are mutable through iterators,
  and changing one breaks the order.
- **Values with throwing moves.** A `static_assert` requires nothrow move
  construction and move assignment.
- **Custom allocators or concurrent access.** Neither is provided; the caller
  bounds input size, scratch memory, and locking.

## ⚡ Performance

ns per `find(stream)`: one lookup of a key drawn at random from a stream of 262,144 keys that
are all in the map, so a branch predictor has no order to learn. Each row names the key and
mapped type (`<u64,string>` is `u64` keys with `std::string` values) and the entry count, the
keys random and distinct. The columns are `flat-map` (`kronuz::flat_map`), the `flat_map` of
[Boost.Container 1.92.0](https://github.com/boostorg/container/tree/boost-1.92.0) (Homebrew,
the same version on both machines) and `std::map`, the node-based tree most code uses instead;
the last column is the Boost time divided by the flat-map time. Lower is faster. Each value is
the best of two runs of the bench, each run the best of 5, and before timing the bench aborts
unless all the maps return the same hit or miss and the same value for every lookup. Compiler
`-O3 -DNDEBUG`, Apple clang 17.

Apple M4 Pro, macOS 26:

| `find(stream)`, ns | flat-map | Boost.Container `flat_map` | `std::map` | flat-map vs Boost |
| --- | ---: | ---: | ---: | ---: |
| `<u32,u32>`, 64 entries | 2.8 | 4.7 | 16.4 | 1.7x faster |
| `<u64,string>`, 1024 entries | 5.0 | 8.8 | 25.8 | 1.7x faster |
| `<u64,u64>`, 4096 entries | 6.4 | 11.2 | 35.2 | 1.8x faster |
| `<u64,u64>`, 65536 entries | 25.9 | 40.1 | 65.1 | 1.5x faster |
| `<u64,u64>`, 1048576 entries | 64.9 | 177 | 195 | 2.7x faster |

Intel Core i9-9980HK, macOS 15 (x86-64):

| `find(stream)`, ns | flat-map | Boost.Container `flat_map` | `std::map` | flat-map vs Boost |
| --- | ---: | ---: | ---: | ---: |
| `<u32,u32>`, 64 entries | 5.6 | 8.1 | 19.3 | 1.5x faster |
| `<u64,string>`, 1024 entries | 9.9 | 41.7 | 32.1 | 4.2x faster |
| `<u64,u64>`, 4096 entries | 14.8 | 53.6 | 42.4 | 3.6x faster |
| `<u64,u64>`, 65536 entries | 51.0 | 117 | 89.7 | 2.3x faster |
| `<u64,u64>`, 1048576 entries | 101 | 237 | 357 | 2.3x faster |

A sorted vector pays at insert: a random-order insert into 4096 `<u64,u64>` entries takes
171 ns in flat-map and 37 ns in `std::map` on the M4 Pro. The two M4 Pro runs of the
1048576-entry row differ by up to 46% per column. [docs/perf/](docs/perf/README.md) has every
row, the insert and replayed-lookup columns, and the spread between the runs.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFLAT_MAP_BUILD_BENCH=ON
cmake --build build --target flat_map_bench
./build/flat_map_bench
```

## 📚 Documentation

- [ARCHITECTURE.md](ARCHITECTURE.md): the contracts, the three searches, and the
  insert shift.
- [docs/perf/](docs/perf/README.md): the full benchmark tables.
- [CHANGELOG.md](CHANGELOG.md): what changed.
- [AGENTS.md](AGENTS.md): rules for changing the search and insert policy.
- [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries): the catalog.

## 🛠️ Building and testing

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

The tests keep their checks in Release builds. Add `-DFLAT_MAP_BUILD_BENCH=ON` to
build `flat_map_bench` and `flat_map_crossover`.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the
catalog's [rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT, Copyright (c) 2026 Germán Méndez Bravo. See [LICENSE](LICENSE).
