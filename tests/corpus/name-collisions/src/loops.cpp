// The same local names (i, n, total) in several functions and as a member.
int sum_a(const int* v, int /*@n_a*/n) {
  int /*@total_a*/total = 0;
  for (int /*@i_a*/i = 0; /*@use_i_a1*/i < /*@use_n_a*/n; ++/*@use_i_a2*/i) /*@use_total_a*/total += v[/*@use_i_a3*/i];
  return /*@use_total_a2*/total;
}
int sum_b(const int* v, int /*@n_b*/n) {
  int /*@total_b*/total = 0;
  for (int /*@i_b*/i = 0; /*@use_i_b1*/i < /*@use_n_b*/n; ++/*@use_i_b2*/i) /*@use_total_b*/total += v[/*@use_i_b3*/i];
  return /*@use_total_b2*/total;
}
struct /*@Holder*/Holder {
  int /*@Holder_i*/i = 0;
  int /*@Holder_n*/n = 1;
  int get_i() const { return /*@use_Holder_i*/i + /*@use_Holder_n*/n; }
  int shadow(int /*@Holder_param_i*/i) const { return /*@use_param_i*/i + this->/*@use_this_i*/i + /*@use_Holder_n2*/n; }
  int loop() const {
    int sum = 0;
    for (int /*@i_h*/i = 0; /*@use_i_h*/i < /*@use_Holder_n3*/n; ++/*@use_i_h2*/i) sum += /*@use_i_h3*/i;   // loop variable hides the member
    return sum + /*@use_this_i2*/i;                                                 // member again
  }
};
