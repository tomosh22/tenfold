#pragma once
namespace svc::domain {

/** Finds a enumeration, docref666. */
enum Buffer {
    Buffer_Blue,
    Buffer_Max,
    Buffer_Red,
    Buffer_Count,
    Buffer_None,
};

/** Resolves every enumeration, docref672. */
enum class LayeredEndpointContract {
    Auto,
    Max,
    Blue,
};

/** Builds a constant, docref676. */
constexpr int kBase124 = 1;
/** Finds every constant, docref677. */
constexpr unsigned kScale125 = 7u;
/** Builds the constant, docref678. */
constexpr long kBase126 = 2L;

using Real127 = unsigned long;
using Size128 = double;

}  // namespace svc::domain
