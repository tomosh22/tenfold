#include "pimpl/cache.h"
struct Cache::/*@Cache_Impl*/Impl {
  int /*@Cache_Impl_size*/size() const { return 9; }
};
Cache::/*@Cache_ctor_def*/Cache() : impl_(new /*@Cache_new_impl*/Impl) {}
Cache::/*@Cache_dtor_def*/~Cache() { delete impl_; }
int Cache::/*@Cache_size_def*/size() const { return impl_->/*@use_Cache_Impl_size*/size(); }
