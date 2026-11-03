#include "engine.h"
int main() {
    /*@use_Engine2*/Engine e;
    e./*@use_render2*/render(3);
    return e./*@use_frames2_main*/frames_ + e./*@use_id2*/id_;
}
