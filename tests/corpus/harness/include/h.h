#pragma once
// Harness corpus (W9-FIX-D): the expectation-file features that exist because the harness itself was wrong or too
// weak. Each block names the limitation it covers; docs/TESTING.md has the format.
struct /*@Widget*/Widget {
  int /*@Widget_size*/size = 0;
  virtual /*@Widget_dtor*/~Widget() = default;
  int /*@Widget_get*/get() const { return size; }
  int /*@Widget_twice*/twice() const { return size * 2; }
};

struct /*@Flags*/Flags {
  unsigned bits;
};
// `~f` below is the unary operator: its anchor must stay on the '~' and the query must be asked there.
inline Flags /*@op_not*/operator~(Flags f) { return Flags{~f.bits}; }

int /*@taken*/taken(int v);
