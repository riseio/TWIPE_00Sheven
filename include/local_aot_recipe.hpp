#pragma once
#include <array>

namespace twine::local_aot::recipe {
#ifdef _WIN32
inline constexpr char target[] = "x86_64-windows-gnu";
#else
inline constexpr char target[] = "x86_64-linux-gnu.2.28";
#endif
inline constexpr std::array compile_flags{
    "-O2", "-DNDEBUG", "-fno-strict-aliasing", "-fexceptions", "-funwind-tables",
    "-msse4.1", "-frounding-math", "-ffp-contract=off", "-fvisibility=hidden", "-fPIC"
};
inline constexpr char c_standard[] = "-std=c17";
inline constexpr char cpp_standard[] = "-std=c++20";
inline constexpr std::array link_flags{"-shared", "-O2"};
#ifndef _WIN32
inline constexpr char no_undefined[] = "-Wl,--no-undefined";
#endif
}
