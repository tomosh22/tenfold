Minimised inputs that once crashed, hung, threw or broke an invariant in a parser, one directory per fuzz harness
(`<target>/<what-it-is>.<ext>`; the directory name selects the harness, `tenfold-fuzz --list` shows them). The unit
tests replay all of them; see docs/TESTING.md, "Fuzzing".
