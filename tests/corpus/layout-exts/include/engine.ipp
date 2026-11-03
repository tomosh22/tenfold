#pragma once

inline int Engine::/*@Engine_step_def*/step(int v) { return /*@use_state_in_step*/state_ += v; }
