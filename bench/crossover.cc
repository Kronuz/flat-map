// Crossover measurements behind the search and insert policy in flat_map.hh
// (flat_map_detail::search_for and shift_with_memmove). ns per operation, best of 5.
//
//   flat_map_crossover search   std::lower_bound vs the branchless search vs the prefetching one,
//                               plus the same loop without the unpredictable hint ("unhinted"),
//                               on a repeating query sequence (cyc: a branch predictor can learn
//                               a short table) and on a long random one (rnd).
//   flat_map_crossover insert   shifting the tail with vector::insert (S), with memmove every time
//                               (M) and with memmove from a minimum tail of 8 to 128 entries (m8..m128).
//
// Re-run it on every platform before changing a constant, with the machine quiet.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "flat_map.hh"

namespace fd = kronuz::flat_map_detail;
static volatile std::uint64_t sink;

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

// The branchless loop with a plain conditional: the compiler may turn the select back into a branch.
template <class P, class K>
static const P* lower_bound_unhinted(const P* e, std::size_t n, const K& k) {
	if (n == 0)
		return e;
	const P* base = e;
	while (n > 1) {
		const std::size_t half = n / 2;
		const P* next = base + half;
		base = (next[-1].first < k) ? next : base;
		n -= half;
	}
	return base + (base->first < k ? 1 : 0);
}

template <class K, class V>
static void search_row(const char* name, std::size_t n) {
	using P = std::pair<K, V>;
	constexpr std::size_t queries = std::size_t(1) << 19;
	std::mt19937_64 rng(n * 7919 + sizeof(P));
	std::set<K> distinct;
	while (distinct.size() < n) distinct.insert(static_cast<K>(rng()));
	std::vector<P> v;
	for (K k : distinct) v.emplace_back(k, V());
	std::vector<K> keys(distinct.begin(), distinct.end());
	auto order = keys;
	std::shuffle(order.begin(), order.end(), rng);
	std::vector<K> cyc(queries), rnd(queries);
	for (std::size_t i = 0; i < queries; ++i) {
		cyc[i] = order[i % n];
		rnd[i] = keys[rng() % n];
	}
	const P* e = v.data();
	auto time = [&](auto kernel, const std::vector<K>& qs) {
		return best_ns(
		    [&] {
			    std::uint64_t s = 0;
			    for (K q : qs) s += std::size_t(kernel(e, n, q) - e);
			    sink = s;
		    },
		    double(queries));
	};
	for (int stream = 0; stream < 2; ++stream) {
		const auto& qs = stream ? rnd : cyc;
		const double s = time([](const P* p, std::size_t m, K k) { return fd::lower_bound_binary(p, m, k, std::less<>{}); }, qs);
		const double u = time([](const P* p, std::size_t m, K k) { return lower_bound_unhinted(p, m, k); }, qs);
		const double b = time([](const P* p, std::size_t m, K k) { return fd::lower_bound_branchless(p, m, k); }, qs);
		const double f = time([](const P* p, std::size_t m, K k) { return fd::lower_bound_prefetch(p, m, k); }, qs);
		std::printf("%-14s sz=%-3zu n=%-8zu %s  std %7.2f  unhinted %7.2f  branchless %7.2f  prefetch %7.2f\n", name, sizeof(P), n,
		            stream ? "rnd" : "cyc", s, u, b, f);
		std::fflush(stdout);
	}
}

// try_emplace into a plain vector, shifting by the strategy under test (min_tail < 0: vector::insert).
template <class K, class V>
struct Mini {
	using P = std::pair<K, V>;
	std::vector<P> v;
	long min_tail;

	void insert_at(std::size_t idx, P&& x) {
		const std::size_t n = v.size();
		if (min_tail >= 0 && n - idx >= std::size_t(min_tail)) {
			v.push_back(x);
			P* d = v.data();
			std::memmove(static_cast<void*>(d + idx + 1), static_cast<const void*>(d + idx), (n - idx) * sizeof(P));
			std::memcpy(static_cast<void*>(d + idx), static_cast<const void*>(&x), sizeof(P));
		} else {
			v.insert(v.begin() + static_cast<std::ptrdiff_t>(idx), std::move(x));
		}
	}
	void try_emplace(K k, V val) {
		if (v.empty() || v.back().first < k) {
			v.emplace_back(k, val);
			return;
		}
		const std::size_t idx = std::size_t(fd::lower_bound_branchless(v.data(), v.size(), k) - v.data());
		if (idx != v.size() && !(k < v[idx].first))
			return;
		insert_at(idx, P(k, val));
	}
};

template <class K, class V, class MakeV>
static void insert_row(const char* name, std::size_t n, MakeV mv) {
	static_assert(fd::bytewise_shift<K, V>);
	std::mt19937_64 rng(n * 31 + sizeof(V));
	std::vector<K> keys(n);
	for (auto& k : keys) k = static_cast<K>(rng());
	const std::size_t outer = std::max<std::size_t>(1, (std::size_t(1) << 18) / n);
	std::printf("%-14s sz=%-3zu n=%-6zu", name, sizeof(std::pair<K, V>), n);
	for (long t : {-1L, 0L, 8L, 16L, 32L, 64L, 128L}) {
		const double ns = best_ns(
		    [&] {
			    for (std::size_t o = 0; o < outer; ++o) {
				    Mini<K, V> m{{}, t};
				    for (K k : keys) m.try_emplace(k, mv(k));
				    sink = sink + m.v.size();
			    }
		    },
		    double(n * outer));
		std::printf("  %s%-3ld %7.2f", t < 0 ? "S" : (t == 0 ? "M" : "m"), t < 0 ? 0L : t, ns);
	}
	std::printf("\n");
	std::fflush(stdout);
}

int main(int argc, char** argv) {
	using u64 = std::uint64_t;
	using u32 = std::uint32_t;
	using i8 = std::int8_t;
	using W8 = std::array<u64, 8>;
	using W32 = std::array<u64, 32>;
	const std::string mode = argc > 1 ? argv[1] : "search";
	if (mode == "search") {
		for (std::size_t n : {16u, 64u, 256u, 1024u, 4096u, 65536u, 1048576u}) search_row<u32, u32>("<u32,u32>", n);
		for (std::size_t n : {16u, 100u, 200u}) search_row<i8, u64>("<i8,u64>", n);
		for (std::size_t n : {16u, 64u, 256u, 1024u, 4096u, 65536u, 1048576u}) search_row<u64, u64>("<u64,u64>", n);
		for (std::size_t n : {64u, 1024u, 16384u, 262144u}) search_row<u64, std::string>("<u64,string>", n);
		for (std::size_t n : {256u, 4096u, 262144u}) search_row<u64, W8>("<u64,u64[8]>", n);
	} else if (mode == "insert") {
		auto id = [](auto k) { return k; };
		for (std::size_t n : {8u, 16u, 32u, 64u, 256u, 1024u, 4096u}) insert_row<u32, u32>("<u32,u32>", n, id);
		for (std::size_t n : {8u, 16u, 32u, 64u, 256u, 1024u, 4096u, 65536u}) insert_row<u64, u64>("<u64,u64>", n, id);
		for (std::size_t n : {16u, 100u}) insert_row<i8, u64>("<i8,u64>", n, [](i8 k) { return u64(k); });
		for (std::size_t n : {16u, 64u, 256u, 1024u, 4096u})
			insert_row<u64, W8>("<u64,u64[8]>", n, [](u64 k) { W8 w{}; w[0] = k; return w; });
		for (std::size_t n : {16u, 64u, 256u, 1024u})
			insert_row<u64, W32>("<u64,u64[32]>", n, [](u64 k) { W32 w{}; w[0] = k; return w; });
	} else {
		std::fprintf(stderr, "usage: %s search|insert\n", argv[0]);
		return 2;
	}
}
