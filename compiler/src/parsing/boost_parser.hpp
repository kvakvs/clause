#pragma once

// Boost 1.90's flags are a bitmask: combinations and zero are valid enum
// values. Tell Clang this semantic fact so its enum checker can analyze library
// calls.
// Boost.Parser includes <windows.h> on Windows; keep its min/max macros away from std::min and std::max.
#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#if defined(__clang__)
namespace boost::parser::detail {
enum class [[clang::flag_enum]] flags : unsigned int;
}
#endif
#include <boost/parser/parser.hpp>
