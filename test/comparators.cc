#include "flat_map.hh"
#include <type_traits>
#include <utility>

struct Direction {
    bool reverse = false;
    bool operator()(int a, int b) const noexcept { return reverse ? a > b : a < b; }
};
struct ThrowSwap {
    bool operator()(int a, int b) const noexcept { return a < b; }
    friend void swap(ThrowSwap&, ThrowSwap&) noexcept(false) {}
};
struct ThrowMove {
    inline static bool refuse = false;
    ThrowMove() = default;
    ThrowMove(const ThrowMove&) = default;
    ThrowMove& operator=(const ThrowMove&) = default;
    ThrowMove(ThrowMove&&) { if (refuse) throw 1; }
    ThrowMove& operator=(ThrowMove&&) { if (refuse) throw 1; return *this; }
    bool operator()(int a, int b) const noexcept { return a < b; }
};
int main() {
    static_assert(!std::is_copy_assignable_v<kronuz::flat_map<int, int, ThrowSwap>>);
    using ThrowingMap = kronuz::flat_map<int, int, ThrowMove>;
    static_assert(!std::is_nothrow_move_constructible_v<ThrowingMap>);
    static_assert(!std::is_nothrow_move_assignable_v<ThrowingMap>);
    static_assert(std::is_nothrow_move_constructible_v<kronuz::flat_map<int, int>>);
    static_assert(std::is_nothrow_move_assignable_v<kronuz::flat_map<int, int>>);
    kronuz::flat_map<int, int, Direction> ascending(Direction{false}), descending(Direction{true});
    ascending.try_emplace(1, 1); ascending.try_emplace(2, 2);
    descending.try_emplace(20, 20); descending.try_emplace(10, 10);
    ascending = descending;
    if (!ascending.key_comp().reverse || ascending.begin()->first != 20 || !ascending.contains(10)) return 1;
    ThrowingMap source;
    source.try_emplace(1, 1);
    ThrowMove::refuse = true;
    try { ThrowingMap moved(std::move(source)); } catch (int) { return 0; }
    return 2;
}
