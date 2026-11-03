#pragma once
namespace deepns {
inline int /*@deep_leaf*/leaf_value() { return 12; }
struct /*@DeepNode*/DeepNode { int /*@DeepNode_depth*/depth = 12; };
}  // namespace deepns
