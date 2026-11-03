#include "pimpl/engine.h"
struct Engine::/*@Engine_Impl*/Impl {
  int /*@Engine_Impl_size*/size() const { return 3; }
  int /*@Engine_Impl_get*/get() const { return 4; }
};
Engine::/*@Engine_ctor_def*/Engine() : impl_(new /*@Engine_new_impl*/Impl) {}
Engine::/*@Engine_dtor_def*/~Engine() { delete impl_; }
int Engine::/*@Engine_size_def*/size() const { return impl_->/*@use_Engine_Impl_size*/size(); }
int Engine::/*@Engine_get_def*/get() const { return impl_->/*@use_Engine_Impl_get*/get(); }
