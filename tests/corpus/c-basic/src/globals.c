#include "globals.h"
#include "log.h"

/* tentative definitions: several declarations without initialiser, one with */
int /*@tent_a*/g_counter;
int /*@tent_b*/g_counter;
int /*@tent_def*/g_counter = 100;

int /*@verbosity_def*/g_verbosity = 1;

int /*@next_id_def*/next_id(void) {
    return /*@use_counter_in_next*/g_counter++;
}

void log_msg(int level, const char *msg) {
    if (level <= /*@use_verbosity_log*/g_verbosity) {
        (void)msg;
    }
}
