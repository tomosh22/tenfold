#ifndef MODULES_CACHE_INCLUDE_CACHE_TYPES_INVOICE_H_
#define MODULES_CACHE_INCLUDE_CACHE_TYPES_INVOICE_H_
namespace svc::cache {

/** Updates the enumeration, docref1253. */
enum class NestedInvoiceEvent {
    /*@Auto_1254_def0*/Auto,
    Last,
    Count,
    Max,
    First,
};

enum class MappedQueue {
    Unknown,
    Green,
    None,
    First,
    Blue,
};

constexpr int kMargin255 = 1;

using Count256 = unsigned long;
using Real257 = unsigned;

}  // namespace svc::cache

#endif  // MODULES_CACHE_INCLUDE_CACHE_TYPES_INVOICE_H_
