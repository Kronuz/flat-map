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

## Performance notes

Integer keys under `std::less<>`/`std::less<Key>` search with a branchless
lower bound (the compare feeds a conditional move; `std::lower_bound` with a
lambda mispredicts about half of its steps). Other keys and comparators keep
`std::lower_bound`. When both key and mapped type are trivially copyable,
insertion shifts the tail with one `memmove` instead of an element-wise move
loop (`std::pair` is not trivially copyable, so the standard library does not
do this itself). Apple arm64, u64 keys, ns per op (random insert / find):

| n | before | now | boost flat_map |
|---|---|---|---|
| 256 | 34.3 / 6.5 | 24.9 / 3.3 | 29.3 / 6.7 |
| 4096 | 290.7 / 9.7 | 167.4 / 6.2 | 172.4 / 11.2 |
| 65536 | 5237 / 35.3 | 3605 / 23.7 | 3587 / 36.4 |

Insertion into the middle stays O(n) by nature; use `flat_map_builder` for bulk
loads. Reproduce with the `flat_map_bench` target.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

CI covers Linux, macOS and FreeBSD with ASan/UBSan. TSan is excluded because the container has no concurrent API. MIT license; see [LICENSE](LICENSE).
