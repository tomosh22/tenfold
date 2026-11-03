#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TYPES_PAYMENT_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TYPES_PAYMENT_H_
namespace svc {
namespace storage {

/** Resolves every enumeration, docref990. */
enum Payment {
    Payment_Unknown,
    Payment_Green,
    Payment_None,
    Payment_Count,
    Payment_Max,
};

/** Updates the constant, docref996. */
constexpr unsigned kCap204 = 7u;
constexpr unsigned kCap205 = 7u;

using Handle206 = unsigned long;
typedef unsigned Handle207;

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TYPES_PAYMENT_H_
