# Changelog

All notable changes to `flat-map`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

## 2026-10-31

### Fixed

- The integer-key search was not branchless on x86. Apple clang compiles the
  loop-carried conditional of the 2026-10-30 search into a branch, so random
  queries ran as slowly as `std::lower_bound` or slower (`<u64,u64>` n=4096:
  54 ns with `std::lower_bound`, 55 ns with the loop). The compare is now marked
  unpredictable (`__builtin_unpredictable` on clang,
  `__builtin_expect_with_probability` on gcc) and the loop is written so the select
  picks between two addresses already computed. Random `find` on the i9: n=4096
  55 -> 14 ns, n=65536 114 -> 62 ns, `<u64,u64[8]>` n=1024 45 -> 10 ns.
- `insert` shifted every tail with a `memmove`: a libc call and a second write of
  the new entry cost more than the element moves below about 32 entries
  (`<u32,u32>` n=16 on the M4 Pro: 11.4 -> 16.6 ns). Tails of 32 entries (and 256
  bytes) or more keep the `memmove`; shorter tails go through `vector::insert`.
- The byte shift is gated on the rule that makes it valid. `std::pair` is not
  trivially copyable, but with trivially copyable and non-`volatile` halves and a
  pair that is trivially copy- and move-constructible and trivially destructible it
  is an implicit-lifetime type, and `memmove` creates the entries it copies
  ([cstring.syn], C++20). The earlier gate tested only the halves. FreeBSD's
  libc++ keeps the copy constructor of `std::pair<unsigned, unsigned>` non-trivial,
  so entries there are moved one by one. No compiler optimises on the difference:
  the copy goes through `void*`.

### Changed

- Tables of 512 KiB or more prefetch both candidates of the next search step:
  `<u64,u64>` n=1048576 on the i9, 255 -> 109 ns; on the M1 Pro, 92 -> 83 ns.
- The search ahead of an insert is the branchless loop without the prefetch, and the
  insert works on an index rather than an iterator, which keeps `try_emplace`
  small enough for its callers to inline.
- `bench/bench.cc` has rows per key width, entry size and table size, a
  `find(stream)` column of random queries beside the replayed `find`, builds without
  abseil or boost, and takes `FLAT_MAP_BENCH_SMALL` and `FLAT_MAP_BENCH_FIND_ONLY`.

### Added

- `test/strategy.cc`: every search kernel against `std::lower_bound` for 9 key
  types, duplicates and extremes; the strategy tables; the shift policy for every
  tail length; a self-pointing entry type and an assignment-counting one that fail
  if the byte shift is forced on; inserts around the threshold against `std::map`.
  The suite fails on each of these mutants: memmove on every tail, no prefetch
  search, prefetch from the first entry, gate forced on, `volatile` clause dropped,
  wrong step in either kernel, last step dropped, comparator ignored. It also fails
  to compile against the 2026-10-30 and 2026-10-24 headers.
- `bench/crossover.cc`, the measurements behind the constants (`search` and
  `insert` modes).

### Notes

- A branch predictor learns `std::lower_bound` on a short table queried in the same
  order again and again. `<i8,u64>` with 100 entries, replayed, takes 5.0 ns with
  `std::lower_bound` and 6.4 ns now on the i9; queried at random it takes 26 and
  6.8 ns. On the M1 Pro the branchless search wins both ways (7.0 -> 4.9 ns replayed,
  10.8 -> 4.8 ns random). The branchless search is kept for every integer
  key; `flat_map_detail::search_for` is the one place to change that.
- On the i9, `insert` into 64 to 256 entries is 27 to 43% slower than on 2026-10-30
  (`<u64,u64>` n=256 24.5 -> 34.6 ns, `<u32,u32>` n=64 14.3 -> 20.4 ns, `<i8,u64>`
  n=100 17.8 -> 22.6 ns) and still faster than on 2026-10-24 (53.6, 23.5 and
  26.5 ns). The cause is not isolated: neither
  the threshold, the search kernel nor the insert path accounts for it. Tables
  of 4096 entries insert 14 to 24% faster than on 2026-10-30; 65536 is level.
- The only mutant the suite cannot see is dropping the unpredictable hint: it
  changes the machine code, not the result. The x86 timings above are its check.

### Performance

ns per operation, `bench/bench.cc`, best of 5, entries as `insert / find /
find(stream)`. 2026-10-24 is the header before the 2026-10-30 changes.
Apple M1 Pro (clang 21), two runs per version, rows at 65536 entries and below
within 11% between runs (`<u64,u64>` n=256 omitted: the first row of a run, up to
86% between runs on this machine):

| row (n) | 2026-10-24 | 2026-10-30 | now |
|---|---|---|---|
| `<u64,u64>` 4096 | 378 / 14.9 / 14.8 | 244 / 9.6 / 9.5 | 245 / 9.4 / 9.4 |
| `<u64,u64>` 65536 | 6936 / 44.1 / 44.1 | 4643 / 31.3 / 29.3 | 4662 / 29.8 / 29.9 |
| `<u64,u64>` 1048576 | - / 136 / 136 | - / 92.0 / 91.1 | - / 83.2 / 84.6 |
| `<u32,u32>` 16 | 15.8 / 3.5 / 5.0 | 19.6 / 2.8 / 2.7 | 16.4 / 2.8 / 2.8 |
| `<u32,u32>` 64 | 17.2 / 6.2 / 6.2 | 20.9 / 4.1 / 4.0 | 18.9 / 4.1 / 4.1 |
| `<i8,u64>` 100 | 23.4 / 7.0 / 10.8 | 23.8 / 4.8 / 4.8 | 21.5 / 4.9 / 4.8 |
| `<u64,u64[8]>` 1024 | 361 / 12.7 / 12.6 | 273 / 8.5 / 8.5 | 274 / 8.6 / 8.6 |
| `<u64,u64[8]>` 262144 | - / 74.1 / 73.2 | - / 60.5 / 60.3 | - / 50.6 / 51.2 |

Apple M4 Pro (clang 17), two runs, rows up to 65536 entries (`<u32,u32>` n=16
insert 11.4 / 16.6 / 11.0 ns for 2026-10-24 / 2026-10-30 / now; n=64 13.9 / 17.6 /
13.7; `<i8,u64>` n=100 18.0 / 20.2 / 17.4; `<u64,u64>` n=256 30.5 / 26.8 / 25.7;
`find` within 3% of 2026-10-30 in every row but `<u32,u32>` n=16: 2.00 -> 2.13 ns).

Intel i9-9980HK (Apple clang 17), one run:

| row (n) | 2026-10-24 | 2026-10-30 | now |
|---|---|---|---|
| `<u64,u64>` 256 | 53.6 / 9.4 / 34.2 | 24.5 / 8.3 / 41.7 | 34.6 / 8.1 / 7.8 |
| `<u64,u64>` 4096 | 580 / 54.0 / 52.8 | 215 / 55.2 / 62.8 | 185 / 14.0 / 14.3 |
| `<u64,u64>` 65536 | 10615 / 114 / 118 | 5367 / 114 / 128 | 5361 / 62.0 / 58.1 |
| `<u64,u64>` 1048576 | - / 246 / 240 | - / 255 / 257 | - / 109 / 110 |
| `<u32,u32>` 16 | 30.6 / 6.6 / 7.3 | 29.9 / 5.8 / 23.2 | 30.4 / 4.3 / 4.0 |
| `<u32,u32>` 64 | 23.5 / 7.5 / 7.6 | 14.3 / 5.9 / 28.3 | 20.4 / 4.9 / 4.8 |
| `<u32,u32>` 4096 | 525 / 17.2 / 17.8 | 119 / 48.4 / 50.6 | 90.4 / 10.7 / 10.7 |
| `<i8,u64>` 100 | 26.5 / 5.0 / 26.3 | 17.8 / 6.7 / 34.7 | 22.6 / 6.4 / 6.8 |
| `<u64,string>` 1024 | 519 / 37.7 / 43.5 | 345 / 35.1 / 53.8 | 314 / 9.3 / 9.4 |
| `<u64,u64[8]>` 1024 | 426 / 39.5 / 43.5 | 234 / 45.4 / 56.2 | 224 / 10.3 / 10.6 |
| `<u64,u64[8]>` 262144 | - / 213 / 217 | - / 233 / 226 | - / 116 / 99.3 |

g++ 16 on the same i9, one run: `find` is 2 to 5 times faster than on 2026-10-24 and
within 10% of 2026-10-30 at 65536 entries and below (gcc kept the conditional move
there; `<u32,u32>` n=4096 replayed 10.2 -> 12.3 ns, streamed 13.1 -> 10.9 ns), and 2.1
times faster at 1048576 entries (247 -> 117 ns). `insert` is within 10% of
2026-10-30 except `<u64,u64[8]>` n=1024 (209 -> 249 ns). One run carries up to 20%
noise on rows below 100 ns.

## 2026-10-30

### Changed

- Integer keys under `std::less<>` or `std::less<Key>` search with a branchless
  lower bound instead of `std::lower_bound`. Other keys and comparators still
  use `std::lower_bound` (`85520ce`).
- When key and mapped type are trivially copyable, insertion shifts the tail
  with one `memmove` instead of an element-wise move loop.

### Added

- `test/search_insert.cc`, a differential test against `std::map` covering
  signed, unsigned, 8-bit, string-mapped and custom-comparator maps.
- `flat_map_bench`, a benchmark against boost `flat_map` and
  `absl::btree_map` (bench target only).

### Performance

Apple arm64, `u64` keys, ns per op (random insert / find):

| n | before | after | boost flat_map |
|---|---|---|---|
| 256 | 34.3 / 6.5 | 24.9 / 3.3 | 29.3 / 6.7 |
| 4096 | 290.7 / 9.7 | 167.4 / 6.2 | 172.4 / 11.2 |
| 65536 | 5237 / 35.3 | 3605 / 23.7 | 3587 / 36.4 |

## 2026-10-24

### Added

- The C++20 sorted-vector map, extracted as a standalone library (`a9a52b0`).
