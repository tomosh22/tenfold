#pragma once
#include <utility>

namespace ov {

struct Buffer {
  int data[4] = {};
  int& /*@at_nc*/at(int i) { return data[i]; }
  const int& /*@at_c*/at(int i) const { return data[i]; }

  int /*@who_l*/who() & { return 1; }
  int /*@who_r*/who() && { return 2; }
  int /*@who_c*/who() const& { return 3; }

  void /*@put_l*/put(const Buffer&) {}
  void /*@put_r*/put(Buffer&&) {}
};

// Base class overload sets: a using-declaration brings the hidden base overloads back.
struct Base {
  int /*@base_f_int*/f(int) { return 0; }
  int /*@base_f_dbl*/f(double) { return 1; }
};
struct Derived : Base {
  using Base::/*@derived_using_f*/f;
  int /*@derived_f_str*/f(const char*) { return 2; }
};
struct Hider : Base {
  int /*@hider_f*/f(const char*) { return 3; }
};

// Virtual overloads: overriding one does not override the other.
struct Animal {
  virtual ~Animal() = default;
  virtual int /*@speak_int*/speak(int) { return 0; }
  virtual int /*@speak_void*/speak() { return 1; }
};
struct Dog : Animal {
  int /*@dog_speak_int*/speak(int) override { return 2; }
};
struct /*@Puppy*/Puppy : Dog {
  int /*@puppy_speak_int*/speak(int) override { return 3; }
  int /*@puppy_speak_void*/speak() override { return 4; }
};

}  // namespace ov
