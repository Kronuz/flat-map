# flat-map

A C++20 sorted-vector map with bulk construction, linear merge traversal and explicit iterator invalidation. Header-only, standard-library dependencies only. Part of [cpp-libraries](https://github.com/Kronuz/cpp-libraries).

```cpp
#include "flat_map.hh"
#include <string>

kronuz::flat_map<std::string, int> values;
kronuz::flat_map_builder<std::string, int> build(values);
build.push("b", 2);
build.push("a", 1);
if (build.finish()) throw std::invalid_argument("duplicate key");
int answer = values.at("a");
```

Link `flat_map::flat_map`. Tests and the demo build only when this repository is the top-level CMake project.

## Contracts

1. Entries are `std::pair<Key, T>` in one `std::vector`. Keys remain mutable through iterators; changing a key can break ordering and is the caller's responsibility. This is a small ordered-map API, not C++23 `std::flat_map` compatibility.
2. Values require nonthrowing move construction and move assignment. Comparator moves must preserve ordering; use nonthrowing comparator moves for a nonthrowing map move. Copy assignment requires nonthrowing comparator swap, constructs a full replacement before commit, and leaves the target unchanged on copy/allocation failure. Old and replacement storage coexist until commit. Assignment of an existing mapped value through `insert_or_assign` has that value type's exception guarantee.
3. Treat every insert or erase as invalidating all borrowed entries and iterators. Reserving capacity does not prevent middle-insertion shifts. Move/swap transfer the vector buffer; references follow that buffer.
4. Single middle insertion/erase moves O(N) entries. Sorted append is amortized O(1); arbitrary-order bulk construction uses the builder. `erase_if` compacts once in O(N) and requires a predicate that does not throw; violating this requirement terminates because the operation is `noexcept`. Repeated positional erasure can be quadratic.
5. A builder borrows its map. After pushing entries, lookup is forbidden until successful `finish()`. A duplicate result is the earliest repeated input index; the construction-only map must be discarded on duplicate or failure. Unsorted construction uses a four-byte index per entry and rejects counts above `uint32_t` range. Sorted input needs no sorting index.
6. `flat_map_merge` traverses two compatibly ordered maps once. Callbacks must not mutate either input. Output ownership and exception cleanup belong to the caller; the function does not roll back callback output.
7. Allocator customization, internal synchronization and budgeted sorting are outside this API. Callers bound input size, scratch memory and execution time. No wire or disk format is defined.

## Search and insertion

A lookup runs one of three searches, picked from the key type and the table size:

- **Integer keys** under `std::less<>` or `std::less<Key>` use a branchless lower
  bound. Each step compares and selects the next base with a conditional move, so a
  query costs one dependent load per level and never mispredicts. The compare is
  marked unpredictable (`__builtin_unpredictable` on clang,
  `__builtin_expect_with_probability` on gcc): without it, clang on x86 turns the
  loop-carried select back into a branch, and the search is no faster than
  `std::lower_bound`. The loop walks pointers and loads through the candidate it
  may select, which keeps one operation off the dependent chain on arm64.
- **A table of 512 KiB or more** (`size() * sizeof(value_type)`) uses the same
  search and prefetches both candidates of the next step, so the cache misses of
  one query overlap instead of chaining. Below that size the extra loads cost 7 to
  12%.
- **Other keys and comparators** use `std::lower_bound`.

A branch predictor can learn `std::lower_bound` when a short table (a few hundred
entries) is queried in the same order again and again: the `find` column in
Performance replays the keys in a fixed order and, on x86, `<i8,u64>` with 100
entries takes 5.0 ns with `std::lower_bound` and 6.4 ns with the branchless search.
On any other query stream (the `find(stream)` column draws keys at random) the
branchless search is 1.6 to 4.6 times faster than `std::lower_bound` on x86 and 1.4 to
2.3 times faster on arm64. `bench/crossover.cc` measures both streams for every
strategy.

Insertion into the middle is O(n); use `flat_map_builder` for bulk loads. The
search ahead of an insert uses the branchless loop without the prefetch, which
keeps `try_emplace` small. A tail of 32 entries (and 256 bytes) or more is shifted
with one `memmove` when the entries allow it, and shorter tails and every other
type go through `std::vector::insert`, which moves entry by entry: below that
length the libc call costs more than the moves.

The byte shift is valid when both halves are trivially copyable and not
`volatile`, and the pair is trivially copy- and move-constructible and trivially
destructible (`flat_map_detail::bytewise_shift`). `std::pair` itself is never
trivially copyable, because its assignment is user-provided, but under those
conditions it is an implicit-lifetime type: `memmove` creates the objects it
copies ([cstring.syn], C++20), and the copied bytes are the values of its
trivially copyable members. Types that are self-referential or registered by
address (`std::string` in libstdc++, `std::list`, anything with a back pointer)
are not trivially copyable and are never shifted by bytes. Whether a given pair
meets the conditions depends on the standard library: FreeBSD's libc++ keeps the
copy constructor of `std::pair<unsigned, unsigned>` non-trivial, so its entries are
always moved one by one.

## Performance

`bench/bench.cc`, ns per operation, best of 5. `insert` is a random-order
`try_emplace` into an empty map; `find` replays the keys in a fixed order;
`find(stream)` draws them at random. Tables above 65536 entries are built sorted and
only searched.

Apple M1 Pro (arm64), Apple clang 21, `-O3`; flat-map / boost `flat_map`:

| entries | insert | find | find(stream) |
|---|---|---|---|
| `<u64,u64>` 4096 | 245 / 246 | 9.4 / 15.0 | 9.4 / 15.0 |
| `<u64,u64>` 65536 | 4662 / 4664 | 29.8 / 42.9 | 29.9 / 42.9 |
| `<u64,u64>` 1048576 | - | 83.2 / 145 | 84.6 / 144 |
| `<u32,u32>` 16 | 16.4 / 21.6 | 2.8 / 3.6 | 2.8 / 5.1 |
| `<u32,u32>` 64 | 18.9 / 22.0 | 4.1 / 6.4 | 4.1 / 6.3 |
| `<i8,u64>` 100 | 21.5 / 24.6 | 4.9 / 7.1 | 4.8 / 11.0 |
| `<u64,string>` 1024 | 281 / 281 | 7.5 / 11.7 | 7.5 / 11.7 |
| `<u64,u64[8]>` 1024 | 274 / 275 | 8.6 / 13.2 | 8.6 / 13.0 |
| `<u64,u64[8]>` 262144 | - | 50.6 / 77.4 | 51.2 / 76.7 |

Intel i9-9980HK (x86_64), Apple clang 17, `-O3`; flat-map / boost `flat_map`:

| entries | insert | find | find(stream) |
|---|---|---|---|
| `<u64,u64>` 256 | 34.6 / 25.0 | 8.1 / 9.4 | 7.8 / 34.4 |
| `<u64,u64>` 4096 | 185 / 208 | 14.0 / 53.8 | 14.3 / 53.9 |
| `<u64,u64>` 65536 | 5361 / 5400 | 62.0 / 115 | 58.1 / 116 |
| `<u64,u64>` 1048576 | - | 109 / 248 | 110 / 243 |
| `<u32,u32>` 16 | 30.4 / 35.5 | 4.3 / 5.7 | 4.0 / 7.3 |
| `<u32,u32>` 64 | 20.4 / 24.4 | 4.9 / 7.7 | 4.8 / 8.1 |
| `<i8,u64>` 100 | 22.6 / 21.6 | 6.4 / 5.2 | 6.8 / 25.2 |
| `<u64,string>` 1024 | 314 / 331 | 9.3 / 40.4 | 9.4 / 42.6 |
| `<u64,u64[8]>` 1024 | 224 / 285 | 10.3 / 43.5 | 10.6 / 43.3 |
| `<u64,u64[8]>` 262144 | - | 116 / 212 | 99.3 / 209 |

The benchmark builds without abseil or boost; abseil adds a `btree_map` row and boost
the `flat_map` row. `FLAT_MAP_BENCH_SMALL=1` skips tables above 65536 entries
and `FLAT_MAP_BENCH_FIND_ONLY=1` times only `find`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target flat_map_bench && ./build/flat_map_bench
cmake --build build --target flat_map_crossover && ./build/flat_map_crossover search
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

`ctest` runs `flat_map_strategy`, which pins the search kernels against `std::lower_bound` and the shift policy. CI covers Linux, macOS and FreeBSD with ASan/UBSan. TSan is excluded because the container has no concurrent API. [CHANGELOG.md](CHANGELOG.md) lists what changed. MIT license; see [LICENSE](LICENSE).
