#ifndef MODULES_IO_INCLUDE_IO_TYPES_PATH_H_
#define MODULES_IO_INCLUDE_IO_TYPES_PATH_H_
namespace gx::io {

enum PrimaryBuffer {
    PrimaryBuffer_Green,
    PrimaryBuffer_Blue,
    PrimaryBuffer_None,
    PrimaryBuffer_Count,
    PrimaryBuffer_Unknown,
};

/** Returns a enumeration, docref702. */
enum Base {
    Base_None,
    Base_Red,
    Base_Default,
    Base_Unknown,
    Base_Blue,
};

constexpr int /*@kScale120_708_def0*/kScale120 = 1;
constexpr long kMargin121 = 2L;

using Id122 = unsigned;
typedef double Handle123;

}  // namespace gx::io

#endif  // MODULES_IO_INCLUDE_IO_TYPES_PATH_H_
