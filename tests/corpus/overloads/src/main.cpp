#include <string>
#include <utility>
#include "ov/arity.h"
#include "ov/convert.h"
#include "ov/namespaces.h"
#include "ov/qualifiers.h"

namespace a { int /*@a_f_int_def*/f(int x) { return x; } }
namespace b { int /*@b_f_dbl_def*/f(double x) { return static_cast<int>(x); } }
namespace gfx { int /*@draw_point_def*/draw(Point p) { return p.x; } }

int main() {
  using namespace ov;
  // ---- arity
  int r0 = /*@use_ar0*/area();
  int r1 = /*@use_ar1*/area(3);
  int r2 = /*@use_ar2*/area(3, 4);
  int (*fp)(int, int) = &/*@use_fp2*/area;                    // address-of picks by target type
  int (*fp1)(int) = static_cast<int (*)(int)>(&/*@use_fp1*/area);
  /*@use_log_c*/log("hello");
  /*@use_log_s*/log(std::string("hello"));
  /*@use_log_c2*/log("hello", 3);

  // ---- template vs. non-template
  int x = 0;
  int p0 = /*@use_pick_i*/pick(1);
  int p1 = /*@use_pick_t*/pick(1.5);
  int p2 = /*@use_pick_p*/pick(&x);
  int p3 = /*@use_pick_expl*/pick<int>(1);                    // explicit arguments exclude the plain function
  int p4 = /*@use_pick_expl_p*/pick<int>(&x);                 // pick<int>(int*) -> T* with T = int

  // ---- qualifiers
  Buffer buf;
  const Buffer cbuf;
  buf./*@use_at_nc*/at(0) = 1;
  int c0 = cbuf./*@use_at_c*/at(0);
  int w0 = buf./*@use_who_l*/who();
  int w1 = Buffer()./*@use_who_r*/who();
  int w2 = cbuf./*@use_who_c*/who();
  int w3 = std::move(buf)./*@use_who_mv*/who();
  buf./*@use_put_l*/put(buf);
  buf./*@use_put_r*/put(Buffer());
  buf./*@use_put_mv*/put(std::move(buf));

  Derived d;
  Hider h;
  int d0 = d./*@use_d_f_int*/f(1);
  int d1 = d./*@use_d_f_dbl*/f(1.5);
  int d2 = d./*@use_d_f_str*/f("s");
  int d3 = h./*@use_h_f*/f("s");
  int d4 = h.Base::/*@use_h_base_f*/f(1);

  Puppy pup;
  Animal& an = pup;
  int s0 = an./*@use_speak_int*/speak(1);
  int s1 = an./*@use_speak_void*/speak();
  Dog dog;
  int s2 = dog./*@use_dog_speak*/speak(1);

  // ---- conversions
  int v0 = /*@use_conv_int*/conv(1);
  int v1 = /*@use_conv_char*/conv('c');                        // char promotes to int
  int v2 = /*@use_conv_float*/conv(1.5f);                      // float promotes to double
  int v3 = /*@use_conv_str*/conv("s");
  int v4 = /*@use_conv_null*/conv(nullptr);                    // only const char* accepts nullptr_t
  int v5 = /*@use_ptrq_zero*/ptrq(0);
  int v6 = /*@use_ptrq_null*/ptrq(nullptr);
  int v7 = /*@use_temp_k*/temp(Kelvin(3.0));
  int v8 = /*@use_temp_c*/temp(Celsius{20.0});                 // via the converting constructor
  int v9 = /*@use_temp_s*/temp("x");

  // ---- initializer lists
  Bag b1 = /*@use_bag_n*/Bag(3);
  Bag b2 = /*@use_bag_nn*/Bag(1, 2);
  Bag b3 = /*@use_bag_il1*/Bag{3};
  Bag b4 = /*@use_bag_il2*/Bag{1, 2};
  Bag* b5 = new /*@use_bag_new*/Bag(7);
  Bag /*@var_bag_a*/ba(3);
  Bag /*@var_bag_b*/bb{1, 2};
  Bag /*@var_bag_c*/bc(1, 2);
  Bag /*@var_bag_d*/bd{3};
  (void)ba; (void)bb; (void)bc; (void)bd;

  // ---- overload sets across namespaces
  int n0 = b::/*@use_b_f_int*/f(1);                            // int -> a::f via the using-declaration
  int n1 = b::/*@use_b_f_dbl*/f(1.5);
  gfx::Point pt{1, 2};
  int n2 = /*@use_draw_point*/draw(pt);                        // argument-dependent lookup
  gfx::Sprite sp{4};
  int n3 = /*@use_draw_sprite*/draw(sp);
  int n4 = c::/*@use_c_f*/f("x");
  (void)r0; (void)r1; (void)r2; (void)fp; (void)fp1; (void)p0; (void)p1; (void)p2; (void)p3; (void)p4; (void)c0;
  (void)w0; (void)w1; (void)w2; (void)w3; (void)d0; (void)d1; (void)d2; (void)d3; (void)d4; (void)s0; (void)s1;
  (void)s2; (void)v0; (void)v1; (void)v2; (void)v3; (void)v4; (void)v5; (void)v6; (void)v7; (void)v8; (void)v9;
  (void)b1; (void)b2; (void)b3; (void)b4; (void)b5; (void)n0; (void)n1; (void)n2; (void)n3; (void)n4;
  return 0;
}
namespace c { int /*@c_f_str_def*/f(const char*) { return 0; } }
