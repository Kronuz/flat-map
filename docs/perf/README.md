# flat-map performance

`bench/bench.cc`, ns per operation, built with `-O3 -DNDEBUG` and Apple clang 17. Each cell
is flat-map / Boost.Container `flat_map` / `std::map`, lower is faster. The comparators are the
`flat_map` of [Boost.Container 1.92.0](https://github.com/boostorg/container/tree/boost-1.92.0)
(Homebrew, `boost` 1.92.0 on both machines) and the `std::map` of Apple's libc++. The bench
also prints `absl::btree_map` rows from abseil 20260817.0 (Homebrew, the same on both
machines); they are not in these tables.

- `insert` is a random-order `try_emplace` into an empty map. Tables above 65536 entries are
  built sorted and only searched (`-`).
- `find` replays the keys in a fixed order: a short period, so a branch predictor can learn
  a small table.
- `find(stream)` draws keys at random from a stream of 262,144 keys that are all in the
  map: nothing to learn. The README quotes this column.
- Rows name the key and mapped type and the entry count; keys are random and distinct.
  `u64[8]` is `std::array<u64, 8>`.
- Each value is the best of two runs of the bench, each run the best of 5 repetitions.
- Before timing a row, the bench builds every map and aborts unless each answers every
  probe like `kronuz::flat_map` (the same hit or miss, the same value). The probes are all
  the keys, the stream and 4096 fresh keys.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DFLAT_MAP_BUILD_BENCH=ON
cmake --build build --target flat_map_bench
./build/flat_map_bench
```

The benchmark builds without abseil or Boost; abseil adds a `btree_map` row and Boost the
`flat_map` row. `FLAT_MAP_BENCH_SMALL=1` skips tables above 65536 entries and
`FLAT_MAP_BENCH_FIND_ONLY=1` times only `find`.

## Apple M4 Pro (arm64), macOS 26, Apple clang 17

| entries | insert | find | find(stream) |
| --- | --- | --- | --- |
| `<u64,u64>` 256 | 25.6 / 27.7 / 22.4 | 3.7 / 6.3 / 6.5 | 3.8 / 6.5 / 21.3 |
| `<u64,u64>` 4096 | 171 / 175 / 37.0 | 6.4 / 11.6 / 26.9 | 6.4 / 11.2 / 35.2 |
| `<u64,u64>` 65536 | 3742 / 3774 / 117 | 27.2 / 40.4 / 64.4 | 25.9 / 40.1 / 65.1 |
| `<u64,u64>` 1048576 | - / - / - | 65.4 / 153 / 211 | 64.9 / 177 / 195 |
| `<u32,u32>` 16 | 11.0 / 15.1 / 17.4 | 2.2 / 2.6 / 2.5 | 2.0 / 3.9 / 8.9 |
| `<u32,u32>` 64 | 13.9 / 17.5 / 20.6 | 2.8 / 4.7 / 4.3 | 2.8 / 4.7 / 16.4 |
| `<u32,u32>` 4096 | 101 / 106 / 46.8 | 6.3 / 11.6 / 26.9 | 6.4 / 11.6 / 35.4 |
| `<i8,u64>` 100 | 17.8 / 20.8 / 20.9 | 3.3 / 4.9 / 4.8 | 3.3 / 8.3 / 17.6 |
| `<string,u64>` 32 | 20.8 / 20.8 / 27.6 | 13.5 / 13.2 / 13.0 | 35.1 / 34.8 / 31.0 |
| `<u64,string>` 1024 | 225 / 231 / 43.3 | 5.0 / 8.6 / 13.4 | 5.0 / 8.8 / 25.8 |
| `<string,string>` 1024 | 394 / 390 / 68.5 | 27.8 / 42.2 / 29.5 | 79.2 / 78.9 / 68.1 |
| `<u64,u64[8]>` 1024 | 207 / 206 / 26.1 | 5.3 / 10.6 / 13.4 | 5.5 / 10.6 / 26.1 |
| `<u64,u64[8]>` 262144 | - / - / - | 50.7 / 103 / 143 | 44.0 / 101 / 159 |

## Intel Core i9-9980HK (x86-64), macOS 15, Apple clang 17

| entries | insert | find | find(stream) |
| --- | --- | --- | --- |
| `<u64,u64>` 256 | 30.0 / 27.3 / 54.6 | 7.3 / 8.7 / 11.1 | 7.3 / 34.3 / 23.9 |
| `<u64,u64>` 4096 | 190 / 213 / 130 | 14.5 / 52.1 / 40.1 | 14.8 / 53.6 / 42.4 |
| `<u64,u64>` 65536 | 5445 / 5450 / 209 | 46.8 / 113 / 90.7 | 51.0 / 117 / 89.7 |
| `<u64,u64>` 1048576 | - / - / - | 106 / 244 / 364 | 101 / 237 / 357 |
| `<u32,u32>` 16 | 25.1 / 34.4 / 53.3 | 4.0 / 5.7 / 3.3 | 5.0 / 7.2 / 11.0 |
| `<u32,u32>` 64 | 19.4 / 25.0 / 54.1 | 4.9 / 7.7 / 6.2 | 5.6 / 8.1 / 19.3 |
| `<u32,u32>` 4096 | 89.6 / 94.2 / 129 | 10.6 / 17.4 / 40.2 | 10.6 / 18.4 / 40.6 |
| `<i8,u64>` 100 | 22.8 / 20.9 / 56.6 | 6.4 / 4.8 / 6.9 | 6.9 / 24.8 / 20.7 |
| `<string,u64>` 32 | 58.8 / 55.4 / 85.8 | 48.9 / 47.5 / 50.6 | 57.0 / 57.6 / 56.0 |
| `<u64,string>` 1024 | 322 / 353 / 153 | 9.8 / 39.6 / 28.1 | 9.9 / 41.7 / 32.1 |
| `<string,string>` 1024 | 561 / 606 / 268 | 135 / 122 / 121 | 127 / 131 / 129 |
| `<u64,u64[8]>` 1024 | 215 / 292 / 125 | 10.3 / 39.8 / 28.4 | 10.6 / 43.0 / 34.2 |
| `<u64,u64[8]>` 262144 | - / - / - | 89.1 / 206 / 262 | 91.1 / 204 / 270 |

## Spread between the two runs

Two runs of the same binary, back to back. The spread of a cell is the slower run over the
faster run, minus 1. On the M4 Pro the median `find(stream)` cell of the rows up to 65536
entries differs by 1.2% between runs and the worst by 8%; the 262144 and 1048576-entry rows
differ by up to 84%, so read those as a range. On the i9 the median is 2.4%, the worst of the
smaller rows 17% and the worst of the large rows 4.1%.

## Replayed and random queries

A branch predictor can learn a binary search when a short table (a few hundred entries) is
queried in the same order again and again. On the i9, `<i8,u64>` with 100 entries, replayed,
takes 4.8 ns in Boost.Container's `flat_map` and 6.4 ns with the branchless search, so
flat-map loses that row. On a random query stream, for integer keys, the branchless search
is 1.4 to 4.7 times faster than Boost.Container's `flat_map` on x86 and 1.5 to 2.7 times
faster on arm64. With `std::string` keys flat-map and Boost.Container's `flat_map` are within
3% of each other, and `std::map` is faster by 12% and 14% on the M4 Pro (`<string,u64>` 32
entries and `<string,string>` 1024 entries): the string compare dominates and a tree does
fewer of them.

## Insert

A sorted vector shifts on every insert, so a random-order insert grows linearly with the
size. On the M4 Pro, 65536 `<u64,u64>` entries cost 3742 ns per insert in flat-map and 117
ns in `std::map`. From 4096 entries up, flat-map inserts as fast as Boost.Container's
`flat_map` or faster. Build a large map from sorted input, or bulk-build it;
`bench/crossover.cc` holds the measurements behind the search and insert constants:

```sh
cmake --build build --target flat_map_crossover
./build/flat_map_crossover search
./build/flat_map_crossover insert
```
