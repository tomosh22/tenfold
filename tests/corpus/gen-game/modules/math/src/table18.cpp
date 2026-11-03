#include "math/table18.h"

namespace gx {
namespace math {

/* Generated data table 18. */
const int /*@kTable18_130_def0*/kTable18[] = {
    6134, 3322, 9866, 6892, 6034, 50, 2460, 6124,
    5598, 7369, 1442, 3616, 9434, 5073, 6549, 9294,
    7743, 8096, 5593, 9698, 8429, 13, 7273, 1166,
    6173, 2129, 6697, 4035, 211, 4109, 1376, 9777,
    9423, 4020, 2268, 4614, 577, 5478, 4113, 954,
    4874, 5446, 7758, 5643, 750, 940, 2290, 6687,
    4261, 3787, 2163, 4184, 3284, 7576, 7376, 6835,
    8070, 5299, 4435, 2980, 2643, 5931, 3736, 664,
    792, 8823, 7186, 752, 1395, 804, 1612, 7786,
    8509, 5964, 8321, 1587, 3159, 1939, 1423, 1129,
    4412, 6313, 9782, 7353, 7078, 9576, 6399, 594,
    6021, 2914, 2491, 2340, 9888, 4401, 8993, 829,
    1273, 7729, 1550, 1890, 7526, 7038, 6343, 5075,
    7240, 1358, 6840, 4918, 5023, 3427, 3559, 6211,
    7367, 9542, 4078, 7577, 5569, 5918, 5616, 5172,
    8084, 9170, 3137, 3917, 8547, 7159, 5618, 2127,
    5054, 4330, 3761, 1215, 2402, 4737, 3730, 4374,
    7963, 8434, 361, 6494, 8391, 7924, 3859, 1847,
    7392, 9586, 827, 3700, 8130, 5294, 5501, 1302,
    5224, 164, 376, 1523, 9631, 4769, 6858, 2272,
    5127, 5277, 1428, 866, 7400, 7054, 4146, 4575,
    9588, 1741, 8089, 3274, 9253, 4623, 1190, 8000,
    3557, 1429, 6985, 2946, 6608, 5029, 9865, 7698,
    817, 8531, 3925, 7144, 3064, 8790, 3726, 9201,
};

int /*@table18_fn_0_131_def0*/table18_fn_0(int v) { return v * 1 + /*@kTable18_130_use0*/kTable18[0]; }
int table18_fn_1(int v) { return v * 2 + /*@kTable18_130_use1*/kTable18[1]; }
int table18_fn_2(int v) { return v * 3 + /*@kTable18_130_use2*/kTable18[2]; }
int table18_fn_3(int v) { return v * 4 + /*@kTable18_130_use3*/kTable18[3]; }
int table18_fn_4(int v) { return v * 5 + /*@kTable18_130_use4*/kTable18[4]; }
int table18_fn_5(int v) { return v * 6 + /*@kTable18_130_use5*/kTable18[5]; }
int table18_fn_6(int v) { return v * 7 + /*@kTable18_130_use6*/kTable18[6]; }
int table18_fn_7(int v) { return v * 1 + /*@kTable18_130_use7*/kTable18[7]; }
int table18_fn_8(int v) { return v * 2 + /*@kTable18_130_use8*/kTable18[0]; }
int table18_fn_9(int v) { return v * 3 + /*@kTable18_130_use9*/kTable18[1]; }
int table18_fn_10(int v) { return v * 4 + /*@kTable18_130_use10*/kTable18[2]; }
int /*@table18_fn_11_142_def0*/table18_fn_11(int v) { return v * 5 + /*@kTable18_130_use11*/kTable18[3]; }

typedef int (*table18_fn_t)(int);
static const table18_fn_t table18_fns[] = {
    &/*@table18_fn_0_131_use0*/table18_fn_0,
    &table18_fn_1,
    &table18_fn_2,
    &table18_fn_3,
    &table18_fn_4,
    &table18_fn_5,
    &table18_fn_6,
    &table18_fn_7,
    &table18_fn_8,
    &table18_fn_9,
    &table18_fn_10,
    &/*@table18_fn_11_142_use1*/table18_fn_11,
};


}  // namespace math
}  // namespace gx
