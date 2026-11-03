// Compiled with: -DA_ON -DB_ON -UB_ON -DVAL=2 -DEMPTY -DCHAIN=VAL  (the last -D/-U of a name wins)
#ifdef A_ON
int /*@a_on_yes*/a_marker = 1;
#else
int /*@a_on_no*/a_marker = 0;
#endif
#ifdef B_ON
int /*@b_on_yes*/b_marker = 1;
#else
int /*@b_on_no*/b_marker = 0;
#endif
#if CHAIN == 2
int /*@chain_two*/chain_marker = 2;
#else
int /*@chain_other*/chain_marker = 0;
#endif
#ifdef EMPTY
int /*@empty_yes*/empty_marker = 1;
#endif
int use_a = /*@use_a_marker*/a_marker + /*@use_b_marker*/b_marker + /*@use_chain_marker*/chain_marker + /*@use_empty_marker*/empty_marker;
