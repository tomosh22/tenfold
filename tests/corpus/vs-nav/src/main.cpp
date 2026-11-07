// vs-nav (VS-NAV): Go To Document on an #include line. The caret is on the opening delimiter of the spelling.
#include /*@inc_quoted*/"util.h"
#include /*@inc_dup*/"dup.h"
#include /*@inc_deep*/"sub/deep.h"
#include /*@inc_angle*/<lib.h>
#include /*@inc_missing*/<no_such_header.h>
#  include   /*@inc_spaced*/ "util.h"

int main() {
  int total = util_value() + dup_value() + deep_value() + lib_value();
  int /*@counter_def*/counter = 0;
  counter = total;
  counter += 2;
  ++counter;
  return /*@counter_use*/counter;
}
