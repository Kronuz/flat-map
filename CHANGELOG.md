# Changelog

All notable changes to `flat-map`. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); the library has no
releases, so entries are dated.

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
