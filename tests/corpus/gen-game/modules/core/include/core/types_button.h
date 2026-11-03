#ifndef MODULES_CORE_INCLUDE_CORE_TYPES_BUTTON_H_
#define MODULES_CORE_INCLUDE_CORE_TYPES_BUTTON_H_
namespace gx::core {

/** Computes a enumeration, docref194. */
enum ActiveBuffer {
    ActiveBuffer_Red,
    ActiveBuffer_Unknown,
    /*@ActiveBuffer_Default_197_def0*/ActiveBuffer_Default,
    ActiveBuffer_Last,
};

/** Finds a enumeration, docref199. */
enum PrimaryCamera {
    PrimaryCamera_Auto,
    PrimaryCamera_None,
    PrimaryCamera_Green,
    PrimaryCamera_Last,
};

constexpr double kMargin35 = 3.5;
constexpr long kLimit36 = 2L;
constexpr long kCap37 = 2L;

using Handle38 = long;

}  // namespace gx::core

#endif  // MODULES_CORE_INCLUDE_CORE_TYPES_BUTTON_H_
