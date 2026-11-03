#pragma once
class /*@Engine*/Engine {
 public:
  /*@Engine_ctor*/Engine();
  /*@Engine_dtor*/~Engine();
  int /*@Engine_size*/size() const;
  int /*@Engine_get*/get() const;
 private:
  struct /*@Engine_Impl_decl*/Impl;
  /*@Engine_impl_type*/Impl* impl_;
};
