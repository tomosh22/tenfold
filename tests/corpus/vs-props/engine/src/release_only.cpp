// Only part of the project when Configuration is Release (conditional ItemGroup entry).
#include "shared.h"
int /*@release_only_fn*/release_only() { return /*@use_level_in_release_only*/level_value(); }
