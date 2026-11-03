int /*@bom2_fn*/bom2_fn(int /*@bom2_param*/v) { return /*@use_bom2_param*/v + 2; }
int /*@bom2_second*/bom2_second(int v) { return bom2_fn(v) * 2; }
