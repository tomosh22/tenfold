// part 3: relies on alpha.cpp and beta.cpp
namespace app {

Report /*@gamma_report_def*/gamma_report(int n) {
    Report r;
    r./*@use_total_gamma*/total = /*@use_beta_total_gamma*/beta_total(n) + /*@use_alpha_total_gamma*/alpha_total(n);
    /*@use_counter_gamma*/g_counter += 1;
    return r;
}

}  // namespace app
