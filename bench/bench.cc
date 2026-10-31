// Benchmark: kronuz::flat_map vs boost::container::flat_map (when found) and
// absl::btree_map, u64 keys. ns per operation, best of 5.
// Build the same file against an older flat_map.hh (-I) for a before/after.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>

#include <absl/container/btree_map.h>

#include "flat_map.hh"
#ifdef HAVE_BOOST
#include <boost/container/flat_map.hpp>
#endif

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

template <class M>
static void row(const char* name, std::size_t n, const std::vector<std::uint64_t>& keys, const std::vector<std::uint64_t>& q) {
	double ins = best_ns([&] { M m; for (auto k : keys) m.emplace(k, k * 3); sink = m.size(); }, double(n));
	M m;
	for (auto k : keys) m.emplace(k, k * 3);
	double find = best_ns([&] { std::uint64_t s = 0; for (auto k : q) s += m.find(k)->second; sink = s; }, double(n));
	std::printf("  n=%-6zu %-24s random insert %8.1f ns   find %6.1f ns\n", n, name, ins, find);
}

int main() {
	for (std::size_t n : {256u, 4096u, 65536u}) {
		std::mt19937_64 rng(n);
		std::vector<std::uint64_t> keys(n);
		for (auto& k : keys) k = rng();
		auto q = keys;
		std::shuffle(q.begin(), q.end(), rng);
		row<kronuz::flat_map<std::uint64_t, std::uint64_t>>("kronuz::flat_map", n, keys, q);
#ifdef HAVE_BOOST
		row<boost::container::flat_map<std::uint64_t, std::uint64_t>>("boost flat_map", n, keys, q);
#endif
		row<absl::btree_map<std::uint64_t, std::uint64_t>>("absl::btree_map", n, keys, q);
	}
}
