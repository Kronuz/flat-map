# flat-map performance

`bench/bench.cc`, ns per operation, best of 5, built with `-O3 -DNDEBUG`.
`insert` is a random-order `try_emplace` into an empty map; `find` replays the
keys in a fixed order; `find(stream)` draws them at random. Tables above 65536
entries are built sorted and only searched. Each cell is flat-map / Boost.Container
`flat_map` from Boost 1.92.0. The abseil row of the benchmark is `absl::btree_map` from abseil
20260817; it is not in these tables.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFLAT_MAP_BUILD_BENCH=ON
cmake --build build --target flat_map_bench
./build/flat_map_bench
```

The benchmark builds without abseil or Boost; abseil adds a `btree_map` row and
Boost the `flat_map` row. `FLAT_MAP_BENCH_SMALL=1` skips tables above 65536
entries and `FLAT_MAP_BENCH_FIND_ONLY=1` times only `find`.

## Apple M4 Pro (arm64), Apple clang 17

| entries | insert | find | find(stream) |
|---|---|---|---|
| `<u64,u64>` 256 | 25.1 / 27.4 | 3.7 / 6.3 | 3.7 / 6.3 |
| `<u64,u64>` 4096 | 167 / 172 | 6.1 / 11.4 | 6.1 / 11.3 |
| `<u64,u64>` 65536 | 3692 / 3692 | 25.8 / 40.9 | 25.8 / 38.2 |
| `<u64,u64>` 1048576 | - | 55.0 / 119 | 69.6 / 148 |
| `<u32,u32>` 16 | 10.9 / 15.6 | 1.8 / 2.7 | 1.8 / 3.9 |
| `<u32,u32>` 64 | 13.7 / 16.7 | 2.7 / 4.9 | 2.7 / 4.5 |
| `<i8,u64>` 100 | 17.0 / 19.7 | 3.1 / 4.9 | 3.1 / 8.3 |
| `<u64,string>` 1024 | 221 / 226 | 5.0 / 8.6 | 5.0 / 8.5 |
| `<u64,u64[8]>` 1024 | 201 / 197 | 5.3 / 10.3 | 5.2 / 10.4 |
| `<u64,u64[8]>` 262144 | - | 39.6 / 95.9 | 37.9 / 77.9 |

## Intel Core i9-9980HK (x86_64), Apple clang 17

| entries | insert | find | find(stream) |
|---|---|---|---|
| `<u64,u64>` 256 | 30.3 / 26.1 | 7.3 / 8.5 | 7.4 / 33.9 |
| `<u64,u64>` 4096 | 186 / 219 | 14.0 / 53.0 | 14.3 / 55.4 |
| `<u64,u64>` 65536 | 5458 / 5431 | 47.3 / 115 | 46.8 / 114 |
| `<u64,u64>` 1048576 | - | 105 / 247 | 109 / 253 |
| `<u32,u32>` 16 | 30.1 / 36.3 | 4.8 / 5.8 | 4.1 / 7.6 |
| `<u32,u32>` 64 | 20.1 / 25.5 | 5.0 / 7.6 | 4.9 / 8.2 |
| `<i8,u64>` 100 | 23.3 / 21.4 | 6.2 / 5.0 | 6.4 / 24.7 |
| `<u64,string>` 1024 | 317 / 344 | 9.5 / 37.0 | 10.3 / 42.2 |
| `<u64,u64[8]>` 1024 | 223 / 338 | 10.3 / 40.3 | 10.6 / 44.8 |
| `<u64,u64[8]>` 262144 | - | 96.5 / 213 | 92.1 / 211 |

## Replayed and random queries

A branch predictor can learn a binary search when a short table (a few hundred
entries) is queried in the same order again and again. On the i9, `<i8,u64>` with
100 entries, replayed, takes 5.0 ns in Boost.Container's `flat_map` and 6.2 ns with the
branchless search. On a random query stream the branchless search is 1.7 to 4.6
times faster than Boost.Container's `flat_map` on x86 and 1.5 to 2.7 times faster on arm64.

`bench/crossover.cc` holds the measurements behind the search and insert
constants:

```sh
cmake --build build --target flat_map_crossover
./build/flat_map_crossover search
./build/flat_map_crossover insert
```
