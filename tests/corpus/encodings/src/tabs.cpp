#include "tabs.h"

int	/*@tab_fn_def*/tab_fn(int	/*@tab_p_def*/p, TabBox &b) {
		return	/*@use_tab_p*/p	+ b.	/*@use_tab_v_in*/v	+	b./*@use_tab_w_in*/w;
}
