#include "ops/vec.h"
#include <ostream>

namespace ops {

Vec& Vec::/*@vec_pluseq_def*/operator+=(const Vec& o) { x += o.x; y += o.y; return *this; }
Vec Vec::/*@vec_neg_def*/operator-() const { return {-x, -y}; }
double& Vec::/*@vec_index_def*/operator[](int i) { return i == 0 ? x : y; }
const double& Vec::/*@vec_index_c_def*/operator[](int i) const { return i == 0 ? x : y; }
double Vec::/*@vec_call1_def*/operator()(double scale) const { return (x + y) * scale; }
double Vec::/*@vec_call2_def*/operator()(double sx, double sy) const { return x * sx + y * sy; }
Vec::/*@vec_to_bool_def*/operator bool() const { return x != 0 || y != 0; }
Vec::/*@vec_to_pair_def*/operator double() const { return x + y; }
Vec /*@vec_plus_def*/operator+(Vec a, const Vec& b) { a += b; return a; }
Vec /*@vec_minus_def*/operator-(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y}; }
Vec /*@vec_mul_vs_def*/operator*(const Vec& v, double s) { return {v.x * s, v.y * s}; }
Vec /*@vec_mul_sv_def*/operator*(double s, const Vec& v) { return v * s; }
std::ostream& /*@vec_shl_def*/operator<<(std::ostream& os, const Vec& v) { return os << v.x << ',' << v.y; }

}  // namespace ops
