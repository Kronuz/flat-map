// Search strategies and tail-shift gates.
//  - every search kernel returns std::lower_bound's answer, for every key width,
//    with duplicates, at every size and at the key extremes;
//  - the strategy tables pick what the crossover measurements found;
//  - the byte-wise tail shift is taken only where it is valid, and never changes a result;
//  - types whose entries must be moved one by one are.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "flat_map.hh"

#define CHECK(...)                                                                               \
	do {                                                                                         \
		if (!(__VA_ARGS__)) {                                                                    \
			std::fprintf(stderr, "%s:%d CHECK failed: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
			std::abort();                                                                        \
		}                                                                                        \
	} while (0)

namespace fd = kronuz::flat_map_detail;
using kronuz::flat_map;

namespace {

// --- the kernels against std::lower_bound ------------------------------------

template <class K>
void check_kernels_on(const std::vector<std::pair<K, int>>& v, const std::vector<K>& queries) {
	const auto* e = v.data();
	const std::size_t n = v.size();
	for (K q : queries) {
		const auto want =
			e + (std::lower_bound(v.begin(), v.end(), q, [](const auto& x, const K& k) { return x.first < k; }) - v.begin());
		CHECK(fd::lower_bound_binary(e, n, q, std::less<>{}) == want);
		CHECK(fd::lower_bound_branchless(e, n, q) == want);
		CHECK(fd::lower_bound_prefetch(e, n, q) == want);
	}
}

template <class K>
void kernels(std::mt19937_64& rng) {
	constexpr K lo = std::numeric_limits<K>::min();
	constexpr K hi = std::numeric_limits<K>::max();
	auto sizes = std::vector<std::size_t>();
	for (std::size_t n = 0; n <= 130; ++n) sizes.push_back(n);
	for (std::size_t n : {255u, 256u, 257u, 511u, 512u, 513u, 1000u, 1023u, 1024u, 1025u, 4097u}) sizes.push_back(n);
	for (std::size_t n : sizes) {
		// Dense or sparse key spaces, with duplicates: lower_bound is defined for any sorted input.
		for (std::uint64_t spread : {std::uint64_t(2), std::uint64_t(40), std::uint64_t(1) << 40}) {
			std::vector<std::pair<K, int>> v(n);
			for (auto& x : v) {
				const std::uint64_t r = rng() % spread;
				x = {static_cast<K>(static_cast<std::int64_t>(r) - (std::is_signed_v<K> ? std::int64_t(spread / 2) : 0)), 0};
			}
			std::sort(v.begin(), v.end());
			std::vector<K> q{lo, hi, static_cast<K>(lo + 1), static_cast<K>(hi - 1), K(0)};
			if constexpr (sizeof(K) == 1) {
				for (int i = lo; i <= hi; ++i) q.push_back(static_cast<K>(i));
			} else {
				for (const auto& x : v) {
					q.push_back(x.first);
					q.push_back(static_cast<K>(x.first - 1));
					q.push_back(static_cast<K>(x.first + 1));
				}
				for (int i = 0; i < 64; ++i) q.push_back(static_cast<K>(rng()));
			}
			check_kernels_on(v, q);
		}
	}
	// A key of every n at its extremes: all entries equal, ascending to the maximum.
	for (std::size_t n : {1u, 2u, 3u, 64u, 65u}) {
		std::vector<std::pair<K, int>> v(n, {hi, 0});
		check_kernels_on(v, {lo, hi, K(0)});
		v.assign(n, {lo, 0});
		check_kernels_on(v, {lo, hi, K(0)});
	}
}

// --- the strategy tables -------------------------------------------------------

template <class K, class T>
constexpr fd::search_kind pick(std::size_t n) {
	return fd::search_for<std::pair<K, T>>(n);
}

void tables() {
	using fd::search_kind;
	using u64 = std::uint64_t;
	using u32 = std::uint32_t;
	CHECK((pick<u64, u64>(0) == search_kind::branchless));
	CHECK((pick<u64, u64>(64) == search_kind::branchless));
	CHECK((pick<u64, u64>(1u << 24) == search_kind::prefetch));
	CHECK((pick<u32, u32>(1u << 24) == search_kind::prefetch));
	// Every integer-keyed table of any size is searched branchlessly, small ones included.
	CHECK((pick<std::int8_t, u64>(100) == search_kind::branchless));
	CHECK((pick<u32, u32>(16) == search_kind::branchless));
	// A table that fits the cache is never prefetched: the extra loads only cost.
	CHECK((pick<u32, u32>(4096) == search_kind::branchless));
	CHECK((pick<u64, u64>(16384) == search_kind::branchless));
	CHECK((pick<u64, std::array<u64, 8>>(4096) == search_kind::branchless));
	// The boundary is the table size in bytes: 512 KiB.
	CHECK((pick<u64, u64>(32767) == search_kind::branchless));
	CHECK((pick<u64, u64>(32768) == search_kind::prefetch));
	// More bytes per entry reach the prefetch size at fewer entries.
	std::size_t narrow = 0, wide = 0;
	while (pick<u64, u64>(narrow) != search_kind::prefetch) ++narrow;
	while (pick<u64, std::array<u64, 8>>(wide) != search_kind::prefetch) ++wide;
	CHECK(wide < narrow);
}

// --- the tail shift -------------------------------------------------------------

struct Quad {
	std::uint64_t a, b, c, d;
};
struct Anchored {  // an entry that must be moved by its own constructor/assignment
	Anchored* self;
	int v;
	Anchored(int x = 0) noexcept : self(this), v(x) {}
	Anchored(const Anchored& o) noexcept : self(this), v(o.v) {}
	Anchored(Anchored&& o) noexcept : self(this), v(o.v) {}
	Anchored& operator=(const Anchored& o) noexcept { v = o.v; return *this; }
	Anchored& operator=(Anchored&& o) noexcept { v = o.v; return *this; }
};
struct Counted {  // counts the element-wise assignments a shift performs
	static inline long assigns = 0;
	int v = 0;
	Counted(int x = 0) noexcept : v(x) {}
	Counted(const Counted&) noexcept = default;
	Counted(Counted&&) noexcept = default;
	Counted& operator=(const Counted& o) noexcept { ++assigns; v = o.v; return *this; }
	Counted& operator=(Counted&& o) noexcept { ++assigns; v = o.v; return *this; }
};

// Never byte-shifted, on any standard library.
static_assert(!fd::bytewise_shift<std::uint64_t, std::string>);
static_assert(!fd::bytewise_shift<std::string, int>);
static_assert(!fd::bytewise_shift<int, Anchored>);
static_assert(!fd::bytewise_shift<int, Counted>);
static_assert(!fd::bytewise_shift<int, volatile int>);  // memmove would access it as non-volatile
static_assert(!fd::bytewise_shift<volatile int, int>);

// Byte-shifted where the standard library gives std::pair a trivial copy and move constructor.
// FreeBSD's libc++ does not (its pair<unsigned, unsigned> is not trivially copy constructible), so the
// gate refuses there and entries are moved one by one; libstdc++ and the other libc++s take the byte path.
#if defined(__GLIBCXX__) || (defined(_LIBCPP_VERSION) && !defined(__FreeBSD__))
static_assert(fd::bytewise_shift<std::uint32_t, std::uint32_t>);
static_assert(fd::bytewise_shift<std::int8_t, std::uint64_t>);
static_assert(fd::bytewise_shift<std::uint64_t, Quad>);
static_assert(fd::bytewise_shift<std::uint64_t, std::array<std::uint64_t, 8>>);
#endif

// The shortest tail shifted by bytes for entries of `entry_bytes` bytes: both minimums must be met.
constexpr std::size_t min_tail(std::size_t entry_bytes) {
	return std::max(fd::shift_memmove_min_entries,
	                (fd::shift_memmove_min_bytes + entry_bytes - 1) / entry_bytes);
}

// A tail shorter than min_tail is never shifted by bytes; a longer one is exactly when the gate allows it.
template <class K, class T>
void expect_policy() {
	const std::size_t m = min_tail(sizeof(std::pair<K, T>));
	if (m > 0)
		CHECK(!fd::shift_with_memmove<K, T>(m - 1));
	CHECK(fd::shift_with_memmove<K, T>(m) == fd::bytewise_shift<K, T>);
	CHECK(fd::shift_with_memmove<K, T>(m + 1000) == fd::bytewise_shift<K, T>);
	CHECK(fd::shift_with_memmove<K, T>(std::size_t(1) << 20) == fd::bytewise_shift<K, T>);
}

void shift_policy() {
	using u32 = std::uint32_t;
	using u8 = std::uint8_t;
	// A tail of a few entries is never worth a libc call (CA6: memmove on every insert cost +56% at 16 entries).
	CHECK(!fd::shift_with_memmove<u32, u32>(1));
	CHECK(!fd::shift_with_memmove<u32, u32>(8));
	CHECK(fd::shift_memmove_min_entries >= 16 && fd::shift_memmove_min_bytes >= 128);
	expect_policy<u32, u32>();
	expect_policy<std::int8_t, std::uint64_t>();
	expect_policy<std::uint64_t, Quad>();
	expect_policy<std::uint64_t, std::array<std::uint64_t, 32>>();
	expect_policy<u8, u8>();
	// Tiny entries need the minimum number of bytes as well as entries.
	CHECK(min_tail(2) >= fd::shift_memmove_min_bytes / 2);
	// Never for a type the gate refuses, however long the tail.
	CHECK(!fd::shift_with_memmove<u32, std::string>(1u << 20));
	CHECK(!fd::shift_with_memmove<int, Anchored>(1u << 20));
	CHECK(!fd::shift_with_memmove<int, volatile int>(1u << 20));
}

template <class K, class T, class Make>
void inserts_straddle_threshold(Make make, std::mt19937_64& rng) {
	// Fill to several multiples of the threshold, inserting at every kind of position:
	// the front (largest tail), the middle, near the end (smallest tail), and random.
	flat_map<K, T> fm;
	std::map<K, T> ref;
	auto both = [&](K k) {
		auto a = fm.try_emplace(k, make(k));
		auto b = ref.try_emplace(k, make(k));
		CHECK(a.second == b.second);
		CHECK(a.first->first == k);
	};
	for (int i = 0; i < 3000; ++i) both(static_cast<K>(10'000 + 2 * i));       // appends
	for (int i = 0; i < 3000; ++i) both(static_cast<K>(9'999 - 2 * i));        // front inserts, tail = everything
	for (int i = 0; i < 3000; ++i) both(static_cast<K>(10'001 + 2 * i));       // each lands before the last tail
	for (int i = 0; i < 4000; ++i) both(static_cast<K>(rng() % 40'000));       // random
	CHECK(fm.size() == ref.size());
	auto it = fm.begin();
	for (const auto& [k, v] : ref) {
		CHECK(it->first == k);
		CHECK(std::memcmp(&it->second, &v, sizeof(T)) == 0);
		++it;
	}
}

void gated_types(std::mt19937_64& rng) {
	// Each insert into a map of up to 3000 entries shifts a tail of up to that many: with a byte-wise
	// shift forced on, an entry that points at itself would keep pointing at its old slot.
	flat_map<int, Anchored> fm;
	for (int i = 0; i < 4000; ++i) fm.try_emplace(static_cast<int>(rng() % 5000), i);
	for (const auto& e : fm) CHECK(e.second.self == &e.second);
	// Counted: the assignments are the shift. A front insert into 100 entries assigns >= 100 of them.
	flat_map<int, Counted> cm;
	cm.reserve(256);  // no reallocation: the insert shifts in place
	for (int i = 100; i < 200; ++i) cm.try_emplace(i, i);
	Counted::assigns = 0;
	cm.try_emplace(0, 0);
	CHECK(Counted::assigns >= 100);
	for (int i = 0; i < 101; ++i) CHECK(std::next(cm.begin(), i)->second.v == (i == 0 ? 0 : 99 + i));
}

}  // namespace

int main() {
	std::mt19937_64 rng(2026);
	kernels<std::int8_t>(rng);
	kernels<std::uint8_t>(rng);
	kernels<std::int16_t>(rng);
	kernels<std::uint16_t>(rng);
	kernels<std::int32_t>(rng);
	kernels<std::uint32_t>(rng);
	kernels<std::int64_t>(rng);
	kernels<std::uint64_t>(rng);
	kernels<char>(rng);
	tables();
	shift_policy();
	inserts_straddle_threshold<std::uint32_t, std::uint32_t>([](std::uint32_t k) { return k * 3u; }, rng);
	inserts_straddle_threshold<std::uint64_t, std::uint64_t>([](std::uint64_t k) { return k * 7u; }, rng);
	inserts_straddle_threshold<std::int8_t, std::uint64_t>([](std::int8_t k) { return std::uint64_t(k) * 5u; }, rng);
	inserts_straddle_threshold<std::uint64_t, Quad>([](std::uint64_t k) { return Quad{k, k + 1, k + 2, k + 3}; }, rng);
	inserts_straddle_threshold<std::uint64_t, std::array<std::uint64_t, 8>>(
		[](std::uint64_t k) { std::array<std::uint64_t, 8> w{}; w[0] = k; w[7] = ~k; return w; }, rng);
	gated_types(rng);
	std::puts("flat_map strategy tests passed");
}
