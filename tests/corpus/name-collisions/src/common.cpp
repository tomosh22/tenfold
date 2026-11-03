#include "common.h"
int /*@Stat_func_def*/Stat(const char*) { return 0; }
int /*@g_get_def*/get(int key) { return key; }
int /*@g_size_def*/size(const char* s) { int n = 0; while (s[n]) ++n; return n; }
void log_ns::/*@log_ns_log_def*/log(int) {}
void /*@g_log_def*/log(const char*) {}
