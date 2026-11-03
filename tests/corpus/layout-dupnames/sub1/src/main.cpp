#include "engine.h"
int main() {
    /*@use_Engine1*/Engine e;
    return e./*@use_tick1*/tick() + e./*@use_count1_main*/count_ + e./*@use_id1*/id_;
}
