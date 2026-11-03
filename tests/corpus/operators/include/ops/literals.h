#pragma once
#include <cstddef>

namespace ops {

struct Length { long double metres; };
struct Name { const char* s; std::size_t n; };

namespace literals {
constexpr Length /*@lit_km*/operator""_km(long double v) { return {v * 1000.0L}; }
constexpr Length /*@lit_m*/operator""_m(long double v) { return {v}; }
constexpr Length /*@lit_m_int*/operator""_m(unsigned long long v) { return {static_cast<long double>(v)}; }
constexpr Name /*@lit_name*/operator""_name(const char* s, std::size_t n) { return {s, n}; }
}  // namespace literals

}  // namespace ops
