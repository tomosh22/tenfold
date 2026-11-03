#include "registry.h"
int Registry::/*@Registry_count_def*/count = 0;
Registry& Registry::/*@Registry_instance_def*/instance() { static Registry r; return r; }
void Registry::/*@Registry_add_def*/add(int v) { ++/*@use_count_in_add*/count; /*@use_instance_in_add*/instance().last_ = v; }
