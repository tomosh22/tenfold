#include "core_api.h"
#include "cfg.h"
#include "arch.h"
#include "log.h"

int a = /*@use_core_level*/core_level();
/*@use_word_t*/word_t w = 0;
int b = /*@use_cfg_value*/cfg_value();
int c = /*@use_arch_name*/arch_name();
int d = /*@use_log_core*/log_line();
int e = /*@use_arch_bits*/arch_bits();
int f = /*@use_runtime*/runtime_kind();
int g = /*@use_compiler_kind*/compiler_kind();
int h = /*@use_core_twice*/core_twice();
int lvl = /*@use_core_level_macro*/CORE_LEVEL;

#ifdef FORCED_TAG
int /*@forced_present*/forced_marker = dbg_forced();
#else
int /*@forced_absent*/forced_marker = 0;
#endif
int i = /*@use_forced_marker*/forced_marker;

#ifdef CORE_DEBUG
int j = /*@use_dbg_helper*/dbg_helper();
int k = /*@use_dbg_forced*/dbg_forced() + /*@use_FORCED_TAG*/FORCED_TAG;
#endif
