#pragma once
// A function named like a struct: the struct then needs the elaborated specifier.
struct /*@Stat*/Stat { int v; };
int /*@Stat_func*/Stat(const char* path);
// The same short names at global scope too.
int /*@g_get*/get(int key);
int /*@g_size*/size(const char* s);
namespace log_ns {
void /*@log_ns_log*/log(int level);
}
void /*@g_log*/log(const char* msg);
