#!/usr/bin/env python3
"""Turn the FAILs of a runner report into `xfail` lines (and the XPASSes back into plain lines) in tenfold-expect.txt.

    xfail_from_report.py CORPUS_DIR REPORT.json [--reason-prefix "..."]

Used once after generating a corpus that is committed: the generator's ground truth stays as it is, every expectation that
tenfold / clangd gets wrong is kept as `xfail ... # reason` (docs/TESTING.md), so the corpus passes (PASS + XFAIL only)
until the engine is fixed and the line starts to XPASS.  Re-run after a regeneration.
"""
import json
import os
import re
import sys


def reason(r):
    exp, act = r.get('expected') or '', r.get('actual') or ''
    if r['kind'] == 'impl' and ('_decl' in act or '_fwd' in act) and '_def' in exp:
        return ('implementation answers the declaration (or a forward declaration) instead of the definition once an earlier go to '
                'definition has opened the declaring header (clangd dynamic index shadows the background index; '
                'STRESS_RESULTS G-1)')
    if r.get('tier') == 'fast' and r['kind'] in ('def', 'decl'):
        return ('fast tier: the scope-aware lexer lookup cannot tell this entity from same-named ones (overloads by '
                'parameter type, explicit specialisations, #if branches, macro-declared names), '
                'docs/ARCHITECTURE.md "Scope-aware lookup in the fast tier"')
    return 'generated-corpus mismatch, see docs/STRESS_RESULTS.md "Generated codebases" findings'


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    corpus, report = sys.argv[1], sys.argv[2]
    with open(report) as fh:
        d = json.load(fh)
    path = os.path.join(corpus, 'tenfold-expect.txt')
    with open(path) as fh:
        lines = fh.read().split('\n')
    changed = 0
    for r in d['results']:
        i = r['line'] - 1
        if r['status'] == 'FAIL' and not lines[i].startswith('xfail ') and ' xfail ' not in lines[i]:
            lines[i] = 'xfail %s  # %s' % (lines[i], reason(r))
            changed += 1
        elif r['status'] == 'XPASS':
            lines[i] = re.sub(r'^xfail ', '', re.sub(r'\s+#.*$', '', lines[i]))
            changed += 1
    with open(path, 'w', newline='\n') as fh:
        fh.write('\n'.join(lines))
    print('%d lines changed in %s' % (changed, path))
    return 0


if __name__ == '__main__':
    sys.exit(main())
