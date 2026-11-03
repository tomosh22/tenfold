#pragma once

namespace gfx {
struct Point { int x, y; };
int /*@draw_point*/draw(Point);
struct Sprite {
  int id;
  friend int /*@draw_sprite_friend*/draw(Sprite s) { return s.id; }  // hidden friend: found only by ADL
};
}  // namespace gfx

namespace a {
int /*@a_f_int*/f(int);
}
namespace b {
int /*@b_f_dbl*/f(double);
using a::/*@b_using_f*/f;  // b::f now names both overloads
}
namespace c {
int /*@c_f_str*/f(const char*);
}
