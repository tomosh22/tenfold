#ifndef MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_QUOTA_H_
#define MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_QUOTA_H_
namespace svc::domain {

enum Util {
    Util_Auto,
    Util_None,
    Util_First,
    Util_Max,
};

/** Returns the enumeration, docref571. */
enum StableCustomer {
    StableCustomer_Unknown,
    StableCustomer_Red,
    StableCustomer_None,
    StableCustomer_Default,
};

constexpr int kLimit103 = 1;
/** Computes the constant, docref577. */
constexpr int kBase104 = 1;

using Size105 = long;
using Handle106 = unsigned long;

}  // namespace svc::domain

#endif  // MODULES_DOMAIN_INCLUDE_DOMAIN_TYPES_QUOTA_H_
