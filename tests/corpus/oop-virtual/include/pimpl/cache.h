#pragma once
class /*@Cache*/Cache {
 public:
  /*@Cache_ctor*/Cache();
  /*@Cache_dtor*/~Cache();
  int /*@Cache_size*/size() const;
 private:
  struct /*@Cache_Impl_decl*/Impl;
  /*@Cache_impl_type*/Impl* impl_;
};
