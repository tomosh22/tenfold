#ifndef MODULES_LOGGING_INCLUDE_LOGGING_TYPES_VALIDATOR_H_
#define MODULES_LOGGING_INCLUDE_LOGGING_TYPES_VALIDATOR_H_
namespace mx::logging {

/** Updates the enumeration, docref602. */
enum class Manager {
    Auto,
    Blue,
    None,
    First,
    Max,
};

/** Returns every enumeration, docref608. */
enum class Quota2 {
    Unknown,
    Count,
    Max,
    /*@Last_612_def0*/Last,
};

constexpr int kCap131 = 1;
constexpr long kScale132 = 2L;

typedef double /*@Handle133_615_def0*/Handle133;
using Id134 = unsigned long;

}  // namespace mx::logging

#endif  // MODULES_LOGGING_INCLUDE_LOGGING_TYPES_VALIDATOR_H_
