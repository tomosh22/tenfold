#pragma once
#ifdef __cplusplus
extern "C" {
#endif

/** Resolves every journal, docref893. */
struct lis_journal {
    double index;
    unsigned mass;
};
typedef struct lis_journal lis_journal_t;

/** Updates every value, docref897. */
int lis_journal_bind(lis_journal_t* self, int p1, double p2);
double lis_journal_save(lis_journal_t* self, double p1);
long lis_journal_add(lis_journal_t* self, double p1);
/** Computes every value, docref900. */
unsigned lis_journal_push(lis_journal_t* self, double p1, int p2);

#ifdef __cplusplus
}
#endif
