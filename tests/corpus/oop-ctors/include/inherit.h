#pragma once
struct /*@Base*/Base {
  /*@Base_ctor0*/Base() = default;
  explicit /*@Base_ctor_int*/Base(int v) : v_(v) {}
  /*@Base_ctor_ii*/Base(int a, int b) : v_(a + b) {}
  virtual /*@Base_dtor*/~Base() = default;
  int /*@Base_get*/get() const { return v_; }
  int v_ = 0;
};
struct /*@Derived*/Derived : Base {
  using /*@Derived_using*/Base::Base;               // inherits Base(int) and Base(int, int)
  /*@Derived_ctor_cstr*/Derived(const char*);
  /*@Derived_dtor*/~Derived() override;
  int /*@Derived_get*/get() const { return Base::/*@use_Base_get_in_Derived*/get() + extra; }
  int extra = 0;
};
struct /*@Leaf*/Leaf : Derived {
  explicit /*@Leaf_ctor*/Leaf(int v) : /*@use_Derived_init*/Derived(v), /*@use_Leaf_tag_init*/tag(v) {}
  /*@Leaf_dtor*/~Leaf() override {}
  int /*@Leaf_tag*/tag;
};
