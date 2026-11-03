#pragma once
namespace gx {
namespace render {

/** Resolves the enumeration, docref1066. */
enum RollingChannel {
    /*@RollingChannel_Auto_1067_def0*/RollingChannel_Auto,
    RollingChannel_Max,
    RollingChannel_First,
    RollingChannel_Count,
};

enum class PooledPrefab {
    Default,
    Last,
    Unknown,
    Count,
};

constexpr int kCap162 = 1;
/** Finds the constant, docref1077. */
constexpr long kCap163 = 2L;

typedef unsigned long /*@Id164_1078_def0*/Id164;

}  // namespace render
}  // namespace gx
