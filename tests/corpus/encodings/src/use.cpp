#include "crlf.h"
#include "bom.h"
#include "latin1.h"
#include "tabs.h"
#include "mb.h"
#include "idents.h"
#include "mixed.h"
#include "seps.h"
#include "bom_ns.h"
extern "C" {
#include "cronly.h"
}
int /*@bom2_fn_decl*/bom2_fn(int v);

int main() {
    CrlfBox cb{1}; BomBox bb{2}; TabBox tb{3, 4}; MbBox mb{5, 6}; données::Größe g{7, 8};
    int r = /*@use_crlf_fn*/crlf_fn(cb) + cb./*@use_crlf_count*/count + /*@use_CRLF_MAX*/CRLF_MAX(1, 2) + /*@use_crlf_after_macro*/crlf_after_macro(1);
    r += /*@use_bom_fn*/bom_fn(bb) + bb./*@use_bom_v*/v + /*@use_bom_g*/bom_g + /*@use_bom2_fn*/bom2_fn(1);
    /*@use_bom_Box*/BomBox bb2{1}; r += /*@use_bb2*/bb2./*@use_bom_v2*/v;
    r += /*@use_l1_fn*/l1_fn("x") + /*@use_l1_after*/l1_after() + /*@use_l1_msg*/l1_msg[0];
    r += /*@use_tab_fn*/tab_fn(1, tb) + tb./*@use_tab_v*/v + tb./*@use_tab_w*/w;
    r += /*@use_mb_fn*/mb_fn("x", 1) + /*@use_mb_emoji*/mb_emoji(1) + mb./*@use_mb_f1*/a + mb./*@use_mb_f2*/b + /*@use_mb_after*/mb_after;
    r += /*@use_id_fn*/fn_ünï(g) + g./*@use_id_kanji*/漢字 + données::/*@use_id_naive*/naïve + /*@use_MAKRO*/MAKRO_Ä;
    données::/*@use_id_Groesse*/Größe g2{1, 2}; (void)g2;
    r += bomns::/*@use_bomns_inner*/inner_bom + (int)bomns::Kind::/*@use_bomns_A*/A + /*@use_bomns_after*/bomns_after();
    r += (int)données::Färg::/*@use_id_Gron*/Grön;
    r += /*@use_mx_fn*/mx_fn(1) + /*@use_mx_other*/mx_other(2) + /*@use_mx_last*/mx_last();
    r += /*@use_sep_after*/sep_after() + /*@use_sep_same*/sep_same() + /*@use_sep_ff*/sep_ff() + /*@use_sep_vt*/sep_vt();
    r += /*@use_cr_fn*/cr_fn(1) + /*@use_cr_g*/cr_g;
    return r;
}
