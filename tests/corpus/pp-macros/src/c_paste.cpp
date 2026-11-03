#include "paste.h"
#include "basic.h"

struct /*@Pair*/Pair {
  int /*@Pair_first*/first;
  int /*@Pair_second*/second;
};

int /*@pasted_ab*/CONCAT_(a, b) = 5;
int use_ab = /*@use_ab*/ab;

#define /*@WORD*/WORD limit
// Without the extra level the arguments are pasted unexpanded: WORD##_x is WORD_x, not limit_x.
int /*@pasted_word*/CONCAT_(WORD, _x) = 0;
int use_word_x = /*@use_word_x*/WORD_x;

// With the indirection the argument is expanded first: WORD becomes limit.
int /*@pasted_limit*/CONCAT(/*@use_WORD_expanded*/WORD, _x) = 1;
int use_limit_x = /*@use_limit_x*/limit_x;

/*@pod4*/DEFINE_POD(4)
/*@pod16*/DEFINE_POD(16)
/*@use_Bytes4*/Bytes4 b4;
/*@use_Bytes16*/Bytes16 b16;

int /*@unique_var*/UNIQUE(tmp_) = 0;
int use_unique = /*@use_unique*/tmp_26;

int field_reads(/*@use_Pair_param*/Pair& p) {
  int a = FIELD_OF(p, /*@use_first_in_macro*/first);
  int b = /*@use_field_of*/FIELD_OF(p, /*@use_second_in_macro*/second);
  return a + b + /*@use_wrap_call*/WRAP_CALL(/*@use_helper_as_arg*/helper_sum, a, b);
}

const char* names[] = {
  /*@use_nameof_a*/NAMEOF(Pair),
  NAMEOF(a b   c),
};
