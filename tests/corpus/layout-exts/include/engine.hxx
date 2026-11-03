#pragma once

class /*@Engine*/Engine {
public:
    int /*@Engine_step_decl*/step(int v);
    int /*@Engine_run_decl*/run(int v);
    int /*@Engine_state_field*/state_ = 0;
};

#include "engine.ipp"
