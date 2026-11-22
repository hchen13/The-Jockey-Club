#pragma once
#include <algorithm>
#include <charconv>
#include <string_view>
#include <vector>

namespace rider_outfits {
// IDs are model-local native preset numbers, matching the F9 labels exactly.
// Presets 0 and 4 were rejected by the user. Invalid pools fail closed, not random.
inline constexpr const char* kMaryBethApproved = "1,2,3,5,6,7";
inline bool MaryBethRejected(int preset) { return preset == 0 || preset == 4; }
inline std::vector<int> Pool(std::string_view value, int count, bool mary)
{
    std::vector<int> pool;
    if (count < 1 || count > 64) return pool;
    while (!value.empty()) {
        const auto comma = value.find(',');
        auto token = value.substr(0, comma);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t')) token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) token.remove_suffix(1);
        int id = -1;
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), id);
        if (token.empty() || parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() || id < 0 || id >= count)
            return {};
        if ((!mary || !MaryBethRejected(id)) && std::find(pool.begin(), pool.end(), id) == pool.end()) pool.push_back(id);
        if (comma == std::string_view::npos) break;
        value.remove_prefix(comma + 1);
        if (value.empty()) return {};
    }
    return pool;
}
inline std::vector<int> MaryBethPool(std::string_view value,int count) {return Pool(value,count,true);}
}
