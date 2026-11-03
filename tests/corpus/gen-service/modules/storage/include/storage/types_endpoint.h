#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TYPES_ENDPOINT_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TYPES_ENDPOINT_H_
namespace svc {
namespace storage {

/** Computes every enumeration, docref1099. */
enum Handle {
    Handle_Max,
    Handle_None,
    Handle_Default,
    Handle_Green,
};

enum class MaskedValidator {
    Red,
    Auto,
    Max,
};

constexpr long kMargin219 = 2L;

using Handle220 = long;

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TYPES_ENDPOINT_H_
