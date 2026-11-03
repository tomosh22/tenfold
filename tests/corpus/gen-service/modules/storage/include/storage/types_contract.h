#ifndef MODULES_STORAGE_INCLUDE_STORAGE_TYPES_CONTRACT_H_
#define MODULES_STORAGE_INCLUDE_STORAGE_TYPES_CONTRACT_H_
namespace svc {
namespace storage {

enum class Job3 {
    Last,
    Max,
    Unknown,
    Default,
    Count,
};

enum class LocalEndpoint {
    Green,
    Last,
    Blue,
    None,
    Auto,
};

/** Resolves every constant, docref1181. */
constexpr double kCap240 = 3.5;

typedef long Handle241;

}  // namespace storage
}  // namespace svc

#endif  // MODULES_STORAGE_INCLUDE_STORAGE_TYPES_CONTRACT_H_
