#ifndef MACROS_H
#define MACROS_H

#define /*@SQUARE_def*/SQUARE(x) ((x) * (x))
#define /*@MAX_def*/MAX(a, b) ((a) > (b) ? (a) : (b))
#define /*@COUNT_OF_def*/COUNT_OF(arr) (sizeof(arr) / sizeof((arr)[0]))
#define /*@LIMIT_def*/LIMIT 16

#endif
