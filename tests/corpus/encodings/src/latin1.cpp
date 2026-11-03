#include "latin1.h"

const char* /*@l1_msg_def*/l1_msg = "Größe éèê";
int /*@l1_fn_def*/l1_fn(const char* s) { return s[0] + /*@use_l1_msg_in*/l1_msg[0]; } // äöü
int /*@l1_after_def*/l1_after(void) { return "éééé" [0] + /*@use_l1_fn_in*/l1_fn("é"); }
