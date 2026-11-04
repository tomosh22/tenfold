// force-included in every translation unit (ForcedIncludeFiles)
#pragma once
#include <cstddef>
inline std::size_t /*@forced_fn*/forced_size() { return sizeof(void*); }
