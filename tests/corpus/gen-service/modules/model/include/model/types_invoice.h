#ifndef MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
#define MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
namespace svc::model {

enum class Mapper {
    None,
    Unknown,
    Green,
    Count,
};

enum CyclicSchedule {
    CyclicSchedule_Red,
    CyclicSchedule_First,
    CyclicSchedule_None,
    CyclicSchedule_Max,
    CyclicSchedule_Count,
};

/** Computes the constant, docref882. */
constexpr double kScale179 = 3.5;
constexpr double kCap180 = 3.5;
/** Updates the constant, docref884. */
constexpr double kBase181 = 3.5;

using Handle182 = unsigned long;
using Real183 = double;

}  // namespace svc::model

#endif  // MODULES_MODEL_INCLUDE_MODEL_TYPES_INVOICE_H_
