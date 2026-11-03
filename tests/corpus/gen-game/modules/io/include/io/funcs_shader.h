#pragma once
namespace gx::io {

/** Updates every result, docref689. */
bool clear(int p0, double p1);
bool process();
bool resetBatch(double p0);
double resetBatch();
/** Builds a result, docref693. */
double /*@init_693_decl0*/init(int p0, double p1);
/** Finds every result, docref694. */
int init(double p0);
/** Returns the result, docref695. */
bool mergeQuest(unsigned p0);

}  // namespace gx::io
