// Benchmark: kronuz::flat_map vs boost::container::flat_map, std::map and absl::btree_map
// (Boost and abseil only when their headers are found). ns per operation, best of 5.
// Before timing a shape, every map is checked to answer every query like kronuz::flat_map
// (same hit or miss, same value) and the run aborts on a difference.
//
//   insert        random-order try_emplace into an empty map (skipped above
//                 65536 entries: a sorted vector shifts O(n) per insert)
//   find          the n keys in shuffled order, repeated: a short period, so a
//                 branch predictor can learn small tables
//   find(stream)  keys drawn at random from a long stream: nothing to learn
//
// Rows cover key width, element width and size, since the search and the insert
// shift each have size-dependent crossovers. Build the same file against another
// flat_map.hh (-I) to compare versions.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#if __has_include(<absl/container/btree_map.h>)
#include <absl/container/btree_map.h>
#define HAVE_ABSL_BTREE 1
#endif
#if __has_include(<boost/container/flat_map.hpp>)
#include <boost/container/flat_map.hpp>
#define HAVE_BOOST_FLAT 1
#endif

#include "flat_map.hh"

static volatile std::uint64_t sink;

// FLAT_MAP_BENCH_FIND_ONLY=1 times only find; FLAT_MAP_BENCH_SMALL=1 skips tables above 65536 entries.
// Both make a quick A/B of the search or the insert possible.
static const bool find_only = std::getenv("FLAT_MAP_BENCH_FIND_ONLY") != nullptr;
static const bool small_only = std::getenv("FLAT_MAP_BENCH_SMALL") != nullptr || find_only;

template <class F>
static double best_ns(F&& f, double ops, int reps = 5) {
	f();
	double best = 1e18;
	for (int i = 0; i < reps; ++i) {
		auto a = std::chrono::steady_clock::now();
		f();
		auto b = std::chrono::steady_clock::now();
		best = std::min(best, std::chrono::duration<double, std::nano>(b - a).count() / ops);
	}
	return best;
}

template <class M, class K, class MakeV>
static void row(const char* name, std::size_t n, const std::vector<K>& keys, const std::vector<K>& q,
                const std::vector<K>& stream, MakeV mv, std::size_t outer) {
	const double ops = double(n) * double(outer);
	double ins = -1;
	if (n <= 65536 && !find_only)
		ins = best_ns(
		    [&] {
			    for (std::size_t o = 0; o < outer; ++o) {
				    M m;
				    for (auto& k : keys) m.try_emplace(k, mv(k));
				    sink = sink + m.size();
			    }
		    },
		    ops);
	auto sorted = keys;
	std::sort(sorted.begin(), sorted.end());
	M m;
	for (auto& k : sorted) m.try_emplace(k, mv(k));
	double find = best_ns(
	    [&] {
		    std::uint64_t s = 0;
		    for (std::size_t o = 0; o < outer; ++o)
			    for (auto& k : q) s += m.find(k) != m.end();
		    sink = sink + s;
	    },
	    ops);
	double rnd = best_ns(
	    [&] {
		    std::uint64_t s = 0;
		    for (auto& k : stream) s += m.find(k) != m.end();
		    sink = sink + s;
	    },
	    double(stream.size()));
	if (ins < 0)
		std::printf("  n=%-7zu %-24s insert        - ns   find %7.2f ns   find(stream) %7.2f ns\n", n, name, find, rnd);
	else
		std::printf("  n=%-7zu %-24s insert %8.2f ns   find %7.2f ns   find(stream) %7.2f ns\n", n, name, ins, find, rnd);
	std::fflush(stdout);
}

// Aborts unless M answers every probe key like the reference map: both miss, or both hit with an
// equal value.
template <class M, class Ref, class K>
static void verify(const char* name, const Ref& ref, const std::vector<K>& sorted, const std::vector<K>& probes) {
	M m;
	for (auto& k : sorted) m.try_emplace(k, ref.find(k)->second);
	for (auto& k : probes) {
		auto a = ref.find(k);
		auto b = m.find(k);
		if ((a == ref.end()) != (b == m.end()) || (a != ref.end() && !(a->second == b->second))) {
			std::fprintf(stderr, "bench: %s answers a lookup differently from kronuz::flat_map\n", name);
			std::abort();
		}
	}
	if (m.size() != ref.size()) {
		std::fprintf(stderr, "bench: %s holds %zu entries, kronuz::flat_map %zu\n", name, std::size_t(m.size()), std::size_t(ref.size()));
		std::abort();
	}
}

// One shape (key type, mapped type, size) against every comparator.
template <class K, class V, class MakeK, class MakeV>
static void shape(const char* label, std::size_t n, std::size_t outer, MakeK mk, MakeV mv) {
	std::mt19937_64 rng(n * 31 + 7);
	if (small_only && n > 65536)
		return;
	std::set<K> distinct;
	while (distinct.size() < n) distinct.insert(mk(rng));
	std::vector<K> keys(distinct.begin(), distinct.end());
	std::shuffle(keys.begin(), keys.end(), rng);
	auto q = keys;
	std::shuffle(q.begin(), q.end(), rng);
	std::vector<K> stream(std::size_t(1) << 18);
	for (auto& k : stream) k = keys[rng() % n];
	std::printf("%s\n", label);
	{
		// Probes: every key, the random stream, and fresh keys (misses, or hits for narrow key types).
		auto sorted = keys;
		std::sort(sorted.begin(), sorted.end());
		kronuz::flat_map<K, V> ref;
		for (auto& k : sorted) ref.try_emplace(k, mv(k));
		auto probes = keys;
		probes.insert(probes.end(), stream.begin(), stream.end());
		for (int i = 0; i < 4096; ++i) probes.push_back(mk(rng));
		verify<std::map<K, V>>("std::map", ref, sorted, probes);
#ifdef HAVE_BOOST_FLAT
		verify<boost::container::flat_map<K, V>>("boost flat_map", ref, sorted, probes);
#endif
#ifdef HAVE_ABSL_BTREE
		verify<absl::btree_map<K, V>>("absl::btree_map", ref, sorted, probes);
#endif
	}
	row<kronuz::flat_map<K, V>>("kronuz::flat_map", n, keys, q, stream, mv, outer);
#ifdef HAVE_BOOST_FLAT
	row<boost::container::flat_map<K, V>>("boost flat_map", n, keys, q, stream, mv, outer);
#endif
	row<std::map<K, V>>("std::map", n, keys, q, stream, mv, outer);
#ifdef HAVE_ABSL_BTREE
	row<absl::btree_map<K, V>>("absl::btree_map", n, keys, q, stream, mv, outer);
#endif
}

int main() {
	using u64 = std::uint64_t;
	using u32 = std::uint32_t;
	using i8 = std::int8_t;
	auto k64 = [](std::mt19937_64& r) { return u64(r()); };
	auto k32 = [](std::mt19937_64& r) { return u32(r()); };
	auto k8 = [](std::mt19937_64& r) { return i8(r()); };
	auto kstr = [](std::mt19937_64& r) { return "key:" + std::to_string(r() % 100000000) + ":suffix"; };
	auto v3 = [](u64 k) { return k * 3; };
	auto vk = [](u32 k) { return k; };
	auto v5 = [](const auto&) { return u64(5); };
	auto vstr = [](const auto&) { return std::string("value-value-value-long-enough"); };
	using W8 = std::array<u64, 8>;
	auto vw8 = [](u64 k) { W8 w{}; w[0] = k; return w; };

	shape<u64, u64>("<u64,u64>", 256, 256, k64, v3);
	shape<u64, u64>("<u64,u64>", 4096, 16, k64, v3);
	shape<u64, u64>("<u64,u64>", 65536, 1, k64, v3);
	shape<u64, u64>("<u64,u64>", 1048576, 1, k64, v3);
	shape<u32, u32>("<u32,u32>", 16, 4000, k32, vk);
	shape<u32, u32>("<u32,u32>", 64, 1000, k32, vk);
	shape<u32, u32>("<u32,u32>", 4096, 16, k32, vk);
	shape<i8, u64>("<i8,u64>", 100, 1000, k8, v5);
	shape<std::string, u64>("<string,u64>", 32, 2000, kstr, v5);
	shape<u64, std::string>("<u64,string>", 1024, 20, k64, vstr);
	shape<std::string, std::string>("<string,string>", 1024, 20, kstr, vstr);
	shape<u64, W8>("<u64,u64[8]>", 1024, 20, k64, vw8);
	shape<u64, W8>("<u64,u64[8]>", 262144, 1, k64, vw8);
}
