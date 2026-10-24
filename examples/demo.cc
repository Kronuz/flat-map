#include "flat_map.hh"
#include <iostream>
#include <string>

int main() {
    kronuz::flat_map<std::string, int> values;
    kronuz::flat_map_builder<std::string, int> builder(values);
    builder.push("second", 2);
    builder.push("first", 1);
    if (builder.finish()) return 1;
    for (const auto& [key, value] : values)
        std::cout << key << ": " << value << '\n';
}
