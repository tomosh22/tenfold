#include "app_cfg.h"
#include "core_api.h"
#include "log.h"

// core_api.h is shared with core, but this project never defines CORE_DEBUG / CORE_PROFILE / CORE_32BIT.
int a = /*@use_app_mode*/app_mode();
int b = /*@use_log_app*/log_line();
int c = /*@use_level_app*/core_level();
/*@use_word_app*/word_t w = 0;
int d = /*@use_runtime_app*/runtime_kind();
int e = /*@use_arch_bits_app*/arch_bits();
int f = /*@use_core_twice_app*/core_twice();
#ifdef APP_BUILD
int /*@app_build_yes*/app_marker = 1;
#else
int /*@app_build_no*/app_marker = 0;
#endif
#ifdef CORE_BUILD
int /*@core_build_leak*/leak_marker = 1;
#else
int /*@core_build_absent*/leak_marker = 0;
#endif
int g = /*@use_app_marker*/app_marker + /*@use_leak_marker*/leak_marker;
