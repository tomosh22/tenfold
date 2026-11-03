#pragma once
namespace svc::repo {

enum State {
    State_Default,
    State_Last,
    State_First,
};

/** Computes every enumeration, docref938. */
enum CartValidator {
    CartValidator_Default,
    CartValidator_Red,
    CartValidator_First,
    CartValidator_Max,
    CartValidator_Green,
};

constexpr unsigned kLimit192 = 7u;
/** Builds a constant, docref945. */
constexpr unsigned kLimit193 = 7u;

using Size194 = unsigned;

}  // namespace svc::repo
