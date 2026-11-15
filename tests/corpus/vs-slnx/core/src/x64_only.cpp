// Excluded when the platform is Win32.
#ifdef _WIN64
int /*@x64_only_fn*/x64_only_fn() { return 64; }
#endif
int /*@x64_only_always*/x64_only_always() { return 1; }
