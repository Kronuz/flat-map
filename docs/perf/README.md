# flat-map performance

`bench/bench.cc`, ns per operation, best of 5, built with `-O3 -DNDEBUG`.
`insert` is a random-order `try_emplace` into an empty map; `find` replays the
keys in a fixed order; `find(stream)` draws them at random. Tables above 65536
entries are built sorted and only searched. Each cell is flat-map / Boost.Container
`flat_map` (the Boost version is not recorded with these runs).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFLAT_MAP_BUILD_BENCH=ON
cmake --build build --target flat_map_bench
./build/flat_map_bench
```

The benchmark builds without abseil or Boost; abseil adds a `btree_map` row and
Boost the `flat_map` row. `FLAT_MAP_BENCH_SMALL=1` skips tables above 65536
entries and `FLAT_MAP_BENCH_FIND_ONLY=1` times only `find`.

## Apple M1 Pro (arm64), Apple clang 21

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

## Intel Core i9-9980HK (x86_64), Apple clang 17

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

## Replayed and random queries

A branch predictor can learn `std::lower_bound` when a short table (a few hundred
entries) is queried in the same order again and again. On the i9, `<i8,u64>` with
100 entries, replayed, takes 5.0 ns with `std::lower_bound` and 6.4 ns with the
branchless search. On a random query stream the branchless search is 1.6 to 4.6
times faster than `std::lower_bound` on x86 and 1.4 to 2.3 times faster on arm64.

`bench/crossover.cc` holds the measurements behind the search and insert
constants:

```sh
cmake --build build --target flat_map_crossover
./build/flat_map_crossover search
./build/flat_map_crossover insert
```
