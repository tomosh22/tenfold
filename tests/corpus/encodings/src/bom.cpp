#include "bom.h"

int /*@bom_g_def*/bom_g = 3;
int /*@bom_fn_def*/bom_fn(const BomBox& b) { return b./*@bom_v_def*/v + /*@use_bom_g_in*/bom_g; }
