#include "types.hh"
#include "engine.hxx"
#include "legacy.hh"
#include "config"

int main() {
    geo::/*@use_Vec2i*/Vec2i v(1, 2);
    geo::/*@use_Vec*/Vec<double> w(1.5, 2.5);
    int d = v./*@use_dot*/dot(v) + v./*@use_len2*/len2() + v./*@use_x*/x + v./*@use_y*/y;
    d += geo::/*@use_scale*/scale(3);

    /*@use_Engine*/Engine e;
    d += e./*@use_run*/run(1) + e./*@use_step*/step(2) + e./*@use_state_main*/state_;

    d += /*@use_legacy_add*/legacy_add(1, 2) + /*@use_legacy_mode_main*/legacy_mode;

    /*@use_Config*/Config c;
    d += /*@use_config_level*/config_level(c) + c./*@use_level_main*/level + /*@use_CONFIG_LEVEL*/CONFIG_LEVEL;
    return d + (int)w./*@use_wx*/x;
}
