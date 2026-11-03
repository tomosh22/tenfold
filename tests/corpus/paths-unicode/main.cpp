#include "src dir/with space.h"
#include "café/résumé.h"
#include "漢字/ソース.h"
#include "emoji 😀/smile 🙂.h"
#include "parens (1)/file (copy).h"
#include "100% pure #1/hash#tag.h"
#include "a+b&c=d/plus+one.h"
#include "[brackets] {braces}/it's @home!.h"
#include "naïve ÅÄÖ ñ/ÜBER straße.h"
#include "level-aaaaaaaaaaaaaaaaaaaaaaaaaaaa/level-bbbbbbbbbbbbbbbbbbbbbbbbbbbb/level-cccccccccccccccccccccccccccc/level-dddddddddddddddddddddddddddd/a_deeply_nested_header_with_a_long_name.h"

int main() {
    int r = 0;
    { /*@sp_S_use*/Box_sp b{0}; r += /*@sp_use*/fn_sp(b) + b./*@sp_val_use*/val; }
    { /*@fr_S_use*/Box_fr b{1}; r += /*@fr_use*/fn_fr(b) + b./*@fr_val_use*/val; }
    { /*@jp_S_use*/Box_jp b{2}; r += /*@jp_use*/fn_jp(b) + b./*@jp_val_use*/val; }
    { /*@em_S_use*/Box_em b{3}; r += /*@em_use*/fn_em(b) + b./*@em_val_use*/val; }
    { /*@pa_S_use*/Box_pa b{4}; r += /*@pa_use*/fn_pa(b) + b./*@pa_val_use*/val; }
    { /*@pc_S_use*/Box_pc b{5}; r += /*@pc_use*/fn_pc(b) + b./*@pc_val_use*/val; }
    { /*@pl_S_use*/Box_pl b{6}; r += /*@pl_use*/fn_pl(b) + b./*@pl_val_use*/val; }
    { /*@br_S_use*/Box_br b{7}; r += /*@br_use*/fn_br(b) + b./*@br_val_use*/val; }
    { /*@de_S_use*/Box_de b{8}; r += /*@de_use*/fn_de(b) + b./*@de_val_use*/val; }
    { /*@lg_S_use*/Box_lg b{9}; r += /*@lg_use*/fn_lg(b) + b./*@lg_val_use*/val; }
    return r;
}
