// One list, expanded as an enum, a name function, a value table and a count.
enum class /*@Color*/Color {
#define X(n, v, l) n = v,
#include "colors.def"
#undef X
};

const char* /*@color_name*/color_name(/*@use_Color_param*/Color /*@c_param*/c) {
  switch (c) {
#define X(n, v, l) case /*@Color_in_case*/Color::n: return l;
#include "colors.def"
#undef X
  }
  return "";
}

struct /*@ColorInfo*/ColorInfo {
  /*@use_Color_in_info*/Color /*@ColorInfo_color*/color;
  int /*@ColorInfo_bits*/bits;
};

const /*@use_ColorInfo_table*/ColorInfo /*@color_table*/color_table[] = {
#define X(n, v, l) { /*@Color_in_table*/Color::n, v },
#include "colors.def"
#undef X
};

enum ColorIndex {
#define X(n, v, l) idx_##n,
#include "colors.def"
#undef X
  /*@idx_count*/idx_count
};

int use_colors() {
  /*@use_Color_c*/Color /*@c_local*/c = Color::/*@use_Red_1*/Red;
  Color d = Color::/*@use_Green_1*/Green;
  const char* name = /*@use_color_name*/color_name(/*@use_c_arg*/c);
  int n = /*@use_idx_count*/idx_count + /*@use_idx_Blue*/idx_Blue;
  int bits = /*@use_table*/color_table[0]./*@use_bits*/bits;
  (void)d; (void)name;
  return (int)Color::/*@use_Blue_1*/Blue + n + bits;
}
