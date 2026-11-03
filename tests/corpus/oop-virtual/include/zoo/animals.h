#pragma once
namespace zoo {
struct /*@Animal*/Animal {
  virtual ~Animal() = default;
  virtual const char* /*@Animal_speak*/speak() const = 0;
  int /*@Animal_legs*/legs = 4;
  int /*@Animal_get*/get() const { return legs; }
};
struct /*@Walker*/Walker : virtual Animal {
  const char* /*@Walker_speak*/speak() const override { return "step"; }
  void /*@Walker_walk*/walk() {}
};
struct /*@Swimmer*/Swimmer : virtual Animal {
  void /*@Swimmer_swim*/swim() {}
};
// Diamond: Duck has one Animal subobject; legs is unambiguous.
struct /*@Duck*/Duck : Walker, Swimmer {
  const char* /*@Duck_speak*/speak() const override { return "quack"; }
};
} // namespace zoo
