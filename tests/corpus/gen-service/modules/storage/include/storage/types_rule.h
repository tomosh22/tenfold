#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TYPES_RULE_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TYPES_RULE_H_
namespace svc {
namespace storage {

enum Context {
    Context_Red,
    Context_Last,
    /*@Context_Blue_1065_def0*/Context_Blue,
    Context_Max,
    Context_Count,
};

/** Finds the enumeration, docref1068. */
enum Mapper2 {
    Mapper2_Last,
    Mapper2_Auto,
    Mapper2_Default,
};

constexpr unsigned kMargin213 = 7u;
/** Computes a constant, docref1073. */
constexpr unsigned kMargin214 = 7u;
/** Finds every constant, docref1074. */
constexpr double kBase215 = 3.5;

using Real216 = double;
using Size217 = double;

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TYPES_RULE_H_
