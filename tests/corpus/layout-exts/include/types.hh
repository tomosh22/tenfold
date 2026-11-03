#pragma once

namespace geo {

template <typename T>
class /*@Vec*/Vec {
public:
    T /*@Vec_x*/x;
    T /*@Vec_y*/y;
    /*@Vec_ctor*/Vec(T x_, T y_);
    T /*@Vec_dot_decl*/dot(const Vec& o) const;
    T /*@Vec_len2_decl*/len2() const;
};

using /*@Vec2i*/Vec2i = Vec<int>;

int /*@scale_decl*/scale(int v);

}  // namespace geo

#include "vec.tpp"
#include "scale.inl"
