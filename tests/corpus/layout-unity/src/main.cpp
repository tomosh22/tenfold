#include "api.h"

int main() {
    app::Report r = app::/*@use_gamma_main*/gamma_report(2);
    return r./*@use_total_main*/total + app::/*@use_beta_main*/beta_total(1) + app::/*@use_alpha_main*/alpha_total(1);
}
