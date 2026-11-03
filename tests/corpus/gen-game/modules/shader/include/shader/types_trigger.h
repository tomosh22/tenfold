#ifndef MODULES_SHADER_INCLUDE_SHADER_TYPES_TRIGGER_H_
#define MODULES_SHADER_INCLUDE_SHADER_TYPES_TRIGGER_H_
namespace gx {
namespace shader {

/** Computes the enumeration, docref1184. */
enum class /*@Policy_1184_def0*/Policy {
    Auto,
    Max,
    First,
    None,
    Default,
};

/** Updates every enumeration, docref1190. */
enum class RadialBoundsBody {
    Auto,
    None,
    Default,
    Last,
    Blue,
};

constexpr double kScale173 = 3.5;
constexpr int kCap174 = 1;

using Size175 = unsigned long;

}  // namespace shader
}  // namespace gx

#endif  // MODULES_SHADER_INCLUDE_SHADER_TYPES_TRIGGER_H_
