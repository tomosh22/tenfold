#include "shared.h"
#if __has_include("feature.h")
#include "feature.h"
#define HAVE_FEATURE 1
#endif

int a = /*@use_level_value*/level_value();
int b = /*@use_sheet_kind*/sheet_kind();
int c = /*@use_common_kind*/common_kind();
int d = /*@use_dirbuild_kind*/dirbuild_kind();
int e = /*@use_fast_kind*/fast_kind();
#ifdef HAVE_FEATURE
int f = /*@use_feature_impl*/feature_impl();
int /*@feature_present*/feature_marker = 1;
#else
int /*@feature_absent*/feature_marker = 0;
#endif
int g = /*@use_feature_marker*/feature_marker;

#ifdef PROJ_engine
int /*@proj_engine_yes*/proj_marker = 1;
#else
int /*@proj_engine_no*/proj_marker = 0;
#endif
#ifdef SLN_vsprops
int /*@sln_yes*/sln_marker = 1;
#else
int /*@sln_no*/sln_marker = 0;
#endif
#ifdef PROJ_EXTRA
int /*@extra_yes*/extra_marker = 1;
#else
int /*@extra_no*/extra_marker = 0;
#endif
#ifdef PLAT_X64
int /*@plat_x64_yes*/plat_marker = 64;
#else
int /*@plat_x64_no*/plat_marker = 32;
#endif
int h = /*@use_proj_marker*/proj_marker + /*@use_sln_marker*/sln_marker + /*@use_extra_marker*/extra_marker + /*@use_plat_marker*/plat_marker;
