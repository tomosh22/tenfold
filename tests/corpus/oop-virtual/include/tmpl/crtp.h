#pragma once
void /*@g_helper*/helper();
template <class T>
struct /*@TBase*/TBase {
  void /*@TBase_helper*/helper() {}
  int /*@TBase_get*/get() const { return 0; }
};
template <class T>
struct /*@TDerived*/TDerived : TBase<T> {
  void run() {
    this->/*@use_this_helper*/helper();   // dependent base: found through this->
    /*@use_unqual_helper*/helper();       // unqualified: the global, bases are not searched in a template
    this->/*@use_this_get*/get();
  }
};
template <class D>
struct /*@Counter*/Counter {
  int /*@Counter_count*/count() const { return 0; }
};
struct /*@Bag*/Bag : Counter<Bag> {
  int /*@Bag_size*/size() const { return 3; }
};
