#pragma once
// List macro style: the list is a macro that takes the per-item macro as its argument.
#define /*@FIELDS*/FIELDS(X) \
  X(int, /*@fl_width*/width) \
  X(int, /*@fl_height*/height) \
  X(double, /*@fl_ratio*/ratio)
#define /*@DECL_FIELD*/DECL_FIELD(t, n) t n;
#define /*@INIT_FIELD*/INIT_FIELD(t, n) n(0),
#define /*@COUNT_FIELD*/COUNT_FIELD(t, n) + 1
