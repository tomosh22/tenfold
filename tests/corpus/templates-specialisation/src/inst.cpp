#include <string>
#include "spec/convert.h"
#include "spec/registry.h"

// Explicit instantiations: the one translation unit that holds the code for these.
template class spec::/*@inst_Registry_int*/Registry<int>;
template class spec::/*@inst_Registry_double*/Registry<double>;
template std::string spec::/*@inst_convert_float*/convert<float>(float);
template std::string spec::/*@inst_checked_int*/convert_checked<int>(int);
