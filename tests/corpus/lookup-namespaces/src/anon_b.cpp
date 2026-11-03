namespace {
int /*@anon_b_helper*/helper() { return 20; }
struct /*@anon_b_Impl*/Impl {
  int /*@anon_b_Impl_get*/get() const { return 2; }
};
} // namespace
static int /*@static_b_tick*/tick() { return 2; }

int anon_b_entry() {
  Impl impl;
  return /*@anon_b_helper_use*/helper() + /*@static_b_tick_use*/tick() + impl./*@anon_b_get_use*/get();
}
