#include "ev/events.h"
#include <functional>

struct Counter {
  int /*@Counter_total*/total = 0;
  int /*@Counter_size*/size() const { return /*@use_total_in_size*/total; }
  int run() {
    auto by_this = [this] { return /*@use_total_this*/total + /*@use_size_this*/size(); };
    auto by_copy = [*this]() mutable { return ++/*@use_total_copy*/total; };
    return by_this() + by_copy();
  }
};

int lambdas() {
  int /*@cap_base*/base = 10;
  int /*@cap_counter*/counter = 0;
  int /*@outer_n*/n = 1;

  auto /*@lam_add*/add = [/*@cap_use_base*/base, &/*@cap_use_counter*/counter](int /*@lam_x*/x) {
    /*@use_add_counter*/counter += /*@use_lam_x*/x;
    return /*@use_cap_base*/base + /*@use_lam_x2*/x;
  };
  int r1 = /*@use_lam_add*/add(5);

  auto /*@lam_init*/init = [/*@init_y*/y = /*@use_base_init*/base * 2](int z) { return /*@use_init_y*/y + z; };
  int r2 = /*@use_lam_init*/init(1);

  auto /*@lam_gen*/gen = [=](auto /*@gen_a*/a, auto b) { return /*@use_gen_a*/a + b + /*@use_cap_default_base*/base; };
  int r3 = /*@use_lam_gen*/gen(1, 2);
  double r4 = /*@use_lam_gen2*/gen(1.5, 2.5);

  auto /*@lam_shadow*/shadow = [](int /*@param_n*/n) { return /*@use_param_n*/n * 2; };   // parameter hides the outer n
  int r5 = shadow(/*@use_outer_n*/n);

  int r6 = [&] { return /*@use_cap_counter_iife*/counter + 1; }();
  int r7 = [](int q) { return q * q; }(3);

  ev::Bus /*@var_bus*/bus;
  ev::Handler /*@var_h*/h = [&/*@cap_h_counter*/counter](int v) { /*@use_h_counter*/counter += v; };
  bus./*@use_Bus_on*/on(/*@use_h_arg*/h);
  bus./*@use_Bus_on2*/on([&](int v) { /*@use_bus_counter*/counter -= v; });
  bus./*@use_Bus_emit*/emit(3);
  /*@use_var_h*/h(4);

  ev::Button /*@var_btn*/btn;
  btn./*@use_on_click_set*/on_click = [&] { ++/*@use_btn_counter*/counter; };
  btn./*@use_on_click_call*/on_click();
  int bs = btn./*@use_Button_size*/size + static_cast<int>(bus./*@use_Bus_size*/size());

  std::function<int(int)> /*@fn_twice*/twice = [](int v) { return v * 2; };
  Counter ctr;
  return r1 + r2 + r3 + (int)r4 + r5 + r6 + r7 + bs + /*@use_fn_twice*/twice(2) + ctr.run() + ctr./*@use_Counter_size*/size() + ctr./*@use_Counter_total*/total;
}
