#include "engine.hxx"

int Engine::/*@Engine_run_def*/run(int v) {
    return /*@use_step_in_run*/step(v) + /*@use_state_in_run*/state_;
}
