#include "tmpl/crtp.h"
void /*@g_helper_def*/helper() {}
void instantiate() {
  TDerived<int> d;
  d.run();
  Bag bag;
  (void)bag./*@use_Counter_count*/count();
  (void)bag./*@use_Bag_size*/size();
}
