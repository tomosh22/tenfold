// Anonymous-namespace and static entities named like the ones in anon_b.cpp: each TU has its own.
namespace {
int /*@anon_a_helper*/helper() { return 10; }
struct /*@anon_a_Impl*/Impl {
  int /*@anon_a_Impl_get*/get() const { return 1; }
};
} // namespace
static int /*@static_a_tick*/tick() { return 1; }

int anon_a_entry() {
  Impl impl;
  return /*@anon_a_helper_use*/helper() + /*@static_a_tick_use*/tick() + impl./*@anon_a_get_use*/get();
}
