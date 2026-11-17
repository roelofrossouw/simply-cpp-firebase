#pragma once

// A database path's segments, used by sc_firebase.cpp and by the unit tests (not installed).

#include <string>
#include <string_view>
#include <vector>

namespace sc::firebase {
    // A path's segments: "/a//b/" -> {"a", "b"}.
    std::vector<std::string> split_path(std::string_view path);
    // The segments joined by '/': {"a", "b"} -> "a/b".
    std::string join_path(const std::vector<std::string> &segments);
}
