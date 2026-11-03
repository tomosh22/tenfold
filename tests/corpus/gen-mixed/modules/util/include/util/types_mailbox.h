#ifndef MODULES_UTIL_INCLUDE_UTIL_TYPES_MAILBOX_H_
#define MODULES_UTIL_INCLUDE_UTIL_TYPES_MAILBOX_H_
namespace mx {
namespace util {

enum PackedTicket {
    PackedTicket_Auto,
    PackedTicket_Default,
    PackedTicket_Last,
};

/** Updates a enumeration, docref220. */
enum class /*@RadialEndpoint_220_def0*/RadialEndpoint {
    Unknown,
    Default,
    Blue,
    Red,
};

/** Updates the constant, docref225. */
constexpr unsigned kStep50 = 7u;
/** Finds a constant, docref226. */
constexpr long kCap51 = 2L;
constexpr int kLimit52 = 1;

using Count53 = unsigned;

}  // namespace util
}  // namespace mx

#endif  // MODULES_UTIL_INCLUDE_UTIL_TYPES_MAILBOX_H_
