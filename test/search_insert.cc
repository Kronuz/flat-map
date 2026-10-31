// Differential tests for the branchless integer search and the memmove insert
// path: every operation is compared against std::map / std::lower_bound over
// random and edge inputs (sizes 0..300, duplicates, extreme values).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "flat_map.hh"

#define CHECK(cond)                                                              \
	do {                                                                         \
		if (!(cond)) {                                                           \
			std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			std::abort();                                                        \
		}                                                                        \
	} while (0)

template <class K, class V, class Cmp = std::less<>>
static void run(std::mt19937_64& rng, std::size_t key_space, auto make_val) {
	for (int round = 0; round < 30; ++round) {
		kronuz::flat_map<K, V, Cmp> fm;
		std::map<K, V, std::conditional_t<std::is_same_v<Cmp, std::less<>>, std::less<K>, Cmp>> ref;
		const int ops = int(rng() % 400);
		for (int i = 0; i < ops; ++i) {
			K k = static_cast<K>(static_cast<std::int64_t>(rng() % key_space) - std::int64_t(key_space / 3));
			if constexpr (std::is_unsigned_v<K>) k = static_cast<K>(rng() % key_space);
			switch (rng() % 5) {
				case 0: { V v = make_val(rng); auto a = fm.try_emplace(k, v); auto b = ref.try_emplace(k, v);
					CHECK(a.second == b.second && a.first->first == b.first->first && a.first->second == b.first->second); break; }
				case 1: { V v = make_val(rng); auto a = fm.insert_or_assign(k, v); auto b = ref.insert_or_assign(k, v);
					CHECK(a.second == b.second && a.first->second == b.first->second); break; }
				case 2: { V v = make_val(rng); auto a = fm.insert({k, v}); auto b = ref.insert({k, v});
					CHECK(a.second == b.second); break; }
				case 3: CHECK(fm.erase(k) == ref.erase(k)); break;
				case 4: { auto a = fm.lower_bound(k); auto b = ref.lower_bound(k);
					CHECK((a == fm.end()) == (b == ref.end())); if (b != ref.end()) CHECK(a->first == b->first);
					auto c = fm.find(k); auto d = ref.find(k);
					CHECK((c == fm.end()) == (d == ref.end())); break; }
			}
		}
		CHECK(fm.size() == ref.size());
		auto it = fm.begin();
		for (auto& [k, v] : ref) { CHECK(it->first == k && it->second == v); ++it; }
		// Probe every key in and around the range, plus the numeric extremes.
		std::vector<K> probes{std::numeric_limits<K>::min(), std::numeric_limits<K>::max()};
		for (std::size_t i = 0; i < key_space + 2; ++i) probes.push_back(static_cast<K>(static_cast<std::int64_t>(i) - std::int64_t(key_space / 3) - 1));
		for (K p : probes) {
			auto a = fm.lower_bound(p); auto b = ref.lower_bound(p);
			CHECK((a == fm.end()) == (b == ref.end()));
			if (b != ref.end()) CHECK(a->first == b->first);
		}
	}
}

int main() {
	std::mt19937_64 rng(42);
	auto u = [](std::mt19937_64& r) { return std::uint64_t(r()); };
	auto s = [](std::mt19937_64& r) { return std::to_string(r() % 1000); };
	for (std::size_t ks : {2u, 7u, 64u, 300u, 5000u}) {
		run<std::uint64_t, std::uint64_t>(rng, ks, u);       // branchless + memmove
		run<std::int64_t, std::uint64_t>(rng, ks, u);
		run<std::int32_t, std::uint64_t>(rng, ks, u);
		run<std::uint8_t, std::uint64_t>(rng, std::min<std::size_t>(ks, 250), u);
		run<std::int8_t, std::uint64_t>(rng, std::min<std::size_t>(ks, 120), u);
		run<std::uint64_t, std::string>(rng, ks, s);          // branchless search, element-wise insert
		run<std::uint64_t, std::uint64_t, std::greater<>>(rng, ks, u);  // custom order: std::lower_bound path
	}
	std::puts("flat_map search/insert differential tests passed");
}
