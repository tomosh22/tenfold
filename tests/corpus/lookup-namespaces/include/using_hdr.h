#pragma once
#include "outer.h"
// using-declarations in a header: app::value is outer::inner::value, app::Node is outer::Node.
namespace app {
using outer::inner::/*@using_value*/value;
using outer::/*@using_Node*/Node;
} // namespace app
