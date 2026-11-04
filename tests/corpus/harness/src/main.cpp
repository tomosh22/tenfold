#include "../include/h.h"

int /*@taken_def*/taken(int v) { return v + 1; }

int main() {
  Widget w;
  int a = w./*@use_get*/get();
  int b = w./*@use_size*/size;
  int c = w./*@use_twice*/twice();
  Flags f{1u};
  Flags g = /*@use_not*/~f;
  Flags h = /*@use_not_call*/operator~(g);
  Widget* p = new Widget;
  p->/*@use_dtor*/~Widget();
  return a + b + c + static_cast<int>(h.bits) + taken(0);
}
