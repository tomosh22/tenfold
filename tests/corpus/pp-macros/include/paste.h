// Token pasting, indirection through a second macro, unique names, stringising.
#pragma once
#define /*@CONCAT_*/CONCAT_(a, b) a##b
#define /*@CONCAT*/CONCAT(a, b) /*@concat_in_concat*/CONCAT_(a, b)
#define /*@UNIQUE*/UNIQUE(prefix) /*@concat_in_unique*/CONCAT(prefix, __LINE__)
#define /*@TYPE_OF_SIZE*/TYPE_OF_SIZE(n) Bytes##n
#define /*@DEFINE_POD*/DEFINE_POD(n) struct TYPE_OF_SIZE(n) { char data[n]; };
#define /*@NAMEOF*/NAMEOF(x) #x
#define /*@FIELD_OF*/FIELD_OF(obj, f) obj.f
#define /*@WRAP_CALL*/WRAP_CALL(fn, ...) fn(__VA_ARGS__)
