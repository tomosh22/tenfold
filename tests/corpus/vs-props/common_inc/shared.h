#pragma once
// Shared by engine and plugin through the CommonInc property of props\common.props.
// Which LEVEL_n macro exists comes from props\<Configuration>.props, a later <Level> in the project, and the platform.
#if defined(LEVEL_9)
inline int /*@level_nine*/level_value() { return 9; }
#elif defined(LEVEL_3)
inline int /*@level_three*/level_value() { return 3; }
#elif defined(LEVEL_1)
inline int /*@level_one*/level_value() { return 1; }
#else
inline int /*@level_none*/level_value() { return 0; }
#endif

#ifdef PROPS_DEBUG
inline int /*@sheet_debug*/sheet_kind() { return 1; }
#elif defined(PROPS_RELEASE)
inline int /*@sheet_release*/sheet_kind() { return 2; }
#else
inline int /*@sheet_none*/sheet_kind() { return 0; }
#endif

#ifdef PROPS_COMMON
inline int /*@common_yes*/common_kind() { return 1; }
#else
inline int /*@common_no*/common_kind() { return 0; }
#endif

#ifdef DIRBUILD_PROPS
inline int /*@dirbuild_yes*/dirbuild_kind() { return 1; }
#else
inline int /*@dirbuild_no*/dirbuild_kind() { return 0; }
#endif

#ifdef FAST_PATH
inline int /*@fast_yes*/fast_kind() { return 1; }
#else
inline int /*@fast_no*/fast_kind() { return 0; }
#endif
