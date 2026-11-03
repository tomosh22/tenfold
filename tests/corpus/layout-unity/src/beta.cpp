// part 2: relies on alpha.cpp having been included before it
namespace app {

int /*@beta_total_def*/beta_total(int n) {
    /*@use_Item_beta*/Item it{ /*@use_bump_beta*/bump(), n };
    return /*@use_alpha_total_beta*/alpha_total(n) + /*@use_scaled_beta*/scaled(it./*@use_weight_beta*/weight) + /*@use_counter_beta*/g_counter;
}

}  // namespace app
