# flat-map architecture

How `kronuz::flat_map` stores entries, what it promises, and how it searches and
inserts. For usage see [README.md](README.md); for measurements see
[docs/perf/](docs/perf/README.md).

## Contracts

1. Entries are `std::pair<Key, T>` in one `std::vector`. Keys remain mutable
   through iterators; changing a key can break ordering and is the caller's
   responsibility. This is a small ordered-map API, not C++23 `std::flat_map`
   compatibility.
2. Values require nonthrowing move construction and move assignment. Comparator
   moves must preserve ordering; use nonthrowing comparator moves for a
   nonthrowing map move. Copy assignment requires nonthrowing comparator swap,
   constructs a full replacement before commit, and leaves the target unchanged on
   copy or allocation failure. Old and replacement storage coexist until commit.
   Assignment of an existing mapped value through `insert_or_assign` has that value
   type's exception guarantee.
3. Treat every insert or erase as invalidating all borrowed entries and iterators.
   Reserving capacity does not prevent middle-insertion shifts. Move and swap
   transfer the vector buffer; references follow that buffer.
4. Single middle insertion or erasure moves O(N) entries. Sorted append is
   amortized O(1); arbitrary-order bulk construction uses the builder. `erase_if`
   compacts once in O(N) and requires a predicate that does not throw; violating
   this requirement terminates because the operation is `noexcept`. Repeated
   positional erasure can be quadratic.
5. A builder borrows its map. After pushing entries, lookup is forbidden until a
   successful `finish()`. A duplicate result is the earliest repeated input index;
   the construction-only map must be discarded on duplicate or failure. Unsorted
   construction uses a four-byte index per entry and rejects counts above the
   `uint32_t` range. Sorted input needs no sorting index.
6. `flat_map_merge` traverses two compatibly ordered maps once. Callbacks must not
   mutate either input. Output ownership and exception cleanup belong to the
   caller; the function does not roll back callback output.
7. Allocator customization, internal synchronization and budgeted sorting are
   outside this API. Callers bound input size, scratch memory and execution time.
   No wire or disk format is defined.

## Search

A lookup runs one of three searches, picked from the key type and the table size:

- **Integer keys** under `std::less<>` or `std::less<Key>` use a branchless lower
  bound. Each step compares and selects the next base with a conditional move, so
  a query costs one dependent load per level and never mispredicts. The compare is
  marked unpredictable (`__builtin_unpredictable` on clang,
  `__builtin_expect_with_probability` on gcc): without the mark, clang on x86 turns
  the loop-carried select back into a branch, and the search is no faster than
  `std::lower_bound`. The loop walks pointers and loads through the candidate it
  may select, which keeps one operation off the dependent chain on arm64.
- **A table of 512 KiB or more** (`size() * sizeof(value_type)`) uses the same
  search and prefetches both candidates of the next step, so the cache misses of
  one query overlap instead of chaining. Below that size the extra loads cost 7 to
  12%.
- **Other keys and comparators** use `std::lower_bound`.

A branch predictor can learn `std::lower_bound` when a short table (a few hundred
entries) is queried in the same order again and again. The trade-offs per query
stream are in [docs/perf/](docs/perf/README.md); `bench/crossover.cc` measures both
streams for every strategy. `flat_map_detail::search_for` is the one place that
picks the search.

## Insertion

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
copy constructor of `std::pair<unsigned, unsigned>` non-trivial, so its entries
are always moved one by one.

`test/strategy.cc` pins every search kernel against `std::lower_bound` and the
shift policy for every tail length.
