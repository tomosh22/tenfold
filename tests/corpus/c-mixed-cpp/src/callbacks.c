#include "codec.h"

int /*@c_checksum_def*/c_checksum(const char *text) {
    int h = 0;
    while (*text) h = h * 31 + *text++;
    return h;
}
