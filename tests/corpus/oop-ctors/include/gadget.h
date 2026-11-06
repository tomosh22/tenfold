#pragma once
// No written constructor: the default and copy constructors are implicit (go to definition offers the class and says so).
struct /*@Gadget*/Gadget { int gadget_n = 1; };
// A written, defaulted constructor is a constructor the user wrote.
struct /*@Sprocket*/Sprocket {
  /*@Sprocket_ctor*/Sprocket() = default;
  explicit /*@Sprocket_ctor_int*/Sprocket(int v) : sprocket_n(v) {}
  int sprocket_n = 0;
};
// Aggregate: braced initialisation calls no constructor at all.
struct /*@Plain*/Plain { int plain_a; int plain_b; };
