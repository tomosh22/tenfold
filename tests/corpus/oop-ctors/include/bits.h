#pragma once
struct /*@Flags*/Flags {
  unsigned /*@Flags_a*/a : 1;
  unsigned /*@Flags_b*/b : 3;
  unsigned : 0;
  unsigned /*@Flags_c*/c : 4;
};
union /*@Variant*/Variant {
  int /*@Variant_i*/i;
  float /*@Variant_f*/f;
};
struct /*@Tagged*/Tagged {
  enum /*@Tagged_Kind*/Kind { /*@Tagged_Int*/IntKind, /*@Tagged_Dbl*/DblKind } kind;
  union {
    int /*@Tagged_i*/i;
    double /*@Tagged_d*/d;
  };
};
