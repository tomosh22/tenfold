#include "shared.h"
// The plugin imports the same sheet through "..\props\common.props"; it has no UseFast, no Level override.
int a = /*@use_level_plugin*/level_value();
int b = /*@use_sheet_plugin*/sheet_kind();
int c = /*@use_common_plugin*/common_kind();
int d = /*@use_dirbuild_plugin*/dirbuild_kind();
int e = /*@use_fast_plugin*/fast_kind();
#ifdef PROJ_plugin
int /*@proj_plugin_yes*/pproj_marker = 1;
#else
int /*@proj_plugin_no*/pproj_marker = 0;
#endif
#ifdef PLUGIN_BUILD
int /*@plugin_build_yes*/pbuild_marker = 1;
#else
int /*@plugin_build_no*/pbuild_marker = 0;
#endif
#ifdef PROJ_EXTRA
int /*@pextra_yes*/pextra_marker = 1;
#else
int /*@pextra_no*/pextra_marker = 0;
#endif
int f = /*@use_pproj_marker*/pproj_marker + /*@use_pbuild_marker*/pbuild_marker + /*@use_pextra_marker*/pextra_marker;
