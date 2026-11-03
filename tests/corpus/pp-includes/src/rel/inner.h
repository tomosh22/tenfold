#pragma once
// Includes resolved relative to this header ("peer.h" next to it) and with "..".
#include "peer.h"
#include "../sibling.h"
inline int /*@inner_fn*/inner_fn() { return /*@use_peer_in_inner*/peer_fn() + /*@use_sibling_in_inner*/sibling_fn(); }
