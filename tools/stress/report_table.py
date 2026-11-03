#!/usr/bin/env python3
"""Turn `tenfold-cli corpus --json` reports (and the .meta.json files of run_generated.sh) into Markdown tables.

    report_table.py REPORT_DIR_OR_FILES...  [--title "..."]

For every `NAME.json` (a runner report) a sibling `NAME.meta.json` (written by run_generated.sh: generator statistics,
wall time, peak RSS of tenfold and clangd) is used when present.  Output: one summary row per run, a latency table (p50 /
p95 per query kind and tier), and the failing expectations grouped by kind.
"""
import argparse
import glob
import json
import os
import sys


def load(path):
    with open(path) as fh:
        return json.load(fh)


def secs(ms):
    return '-' if ms is None or ms < 0 else ('%.1f' % (ms / 1000.0))


def fmt_ms(x):
    if x is None:
        return '-'
    return '%.2f' % x if x < 10 else '%.0f' % x


def collect(args):
    files = []
    for a in args:
        if os.path.isdir(a):
            files += sorted(glob.glob(os.path.join(a, '*.json')))
        else:
            files.append(a)
    runs = []
    for f in files:
        if f.endswith('.meta.json') or f.endswith('.stats.json'):
            continue
        try:
            d = load(f)
        except (OSError, ValueError):
            continue
        if 'counts' not in d or 'results' not in d:
            continue
        base = f[:-5]
        meta = load(base + '.meta.json') if os.path.exists(base + '.meta.json') else {}
        runs.append((os.path.basename(base), d, meta))
    return runs


def summary_table(runs):
    out = ['| run | files | LOC | start+fast index (s) | first TU (s) | clangd index (s) | total (s) | pass / fail / xfail / xpass | '
           'fast==final | peak RSS tenfold / clangd (MiB) |',
           '|---|---:|---:|---:|---:|---:|---:|---|---:|---|']
    for name, d, meta in runs:
        c = d['counts']
        gen = meta.get('gen', {})
        t = d.get('timingMs', {})
        fa = d.get('fastAgreement', {})
        rss = meta.get('rssMiB', {})
        out.append('| %s | %s | %s | %s | %s | %s | %s | %d / %d / %d / %d | %s | %s / %s |' % (
            name, gen.get('files', '-'), gen.get('loc', '-'), secs((t.get('start') or 0) + (t.get('fastIndex') or 0)),
            secs(t.get('firstTu')), secs(t.get('clangdIndex')), secs(t.get('total')), c['pass'], c['fail'], c['xfail'],
            c['xpass'], ('%d/%d (%.0f%%)' % (fa['agree'], fa['queries'], 100 * fa['rate'])) if fa.get('queries') else '-',
            '%.0f' % rss.get('main', d.get('peakRssMiB', 0)), ('%.0f' % rss['clangd']) if 'clangd' in rss else '-'))
    return out


KINDS = ['def', 'decl', 'typedef', 'impl', 'refs', 'hover', 'sym', 'rename']


def latency_table(runs):
    out = ['| run | ' + ' | '.join('%s fast p50/p95' % k if k in ('def', 'decl', 'hover', 'sym') else '' for k in KINDS if k in
                                  ('def', 'decl', 'hover', 'sym')) + ' | ' +
           ' | '.join('%s final p50/p95' % k for k in KINDS) + ' |']
    fk = [k for k in KINDS if k in ('def', 'decl', 'hover', 'sym')]
    out.append('|---|' + '---:|' * (len(fk) + len(KINDS)))
    for name, d, meta in runs:
        lat = d.get('latencyMs', {})
        cells = []
        for tier, ks in (('fast', fk), ('final', KINDS)):
            for k in ks:
                v = lat.get('%s.%s' % (k, tier))
                cells.append('-' if not v else '%s / %s (%d)' % (fmt_ms(v['median']), fmt_ms(v['p95']), v['n']))
        out.append('| %s | %s |' % (name, ' | '.join(cells)))
    return out


def failures(runs, limit=12):
    out = []
    for name, d, meta in runs:
        bad = [r for r in d['results'] if r['status'] in ('FAIL', 'XPASS')]
        if not bad:
            continue
        by = {}
        for r in bad:
            by.setdefault((r['status'], r['kind'], r.get('tier', '')), []).append(r)
        out.append('**%s**: %d failing expectations' % (name, len(bad)))
        out.append('')
        out.append('| status | kind | tier | count | first example |')
        out.append('|---|---|---|---:|---|')
        for (st, k, tier), rs in sorted(by.items()):
            ex = rs[0]
            out.append('| %s | %s | %s | %d | `%s` -> %s |' % (st, k, tier, len(rs), ex['text'][:90].replace('|', '\\|'),
                                                              (ex.get('actual') or '')[:90].replace('|', '\\|')))
        out.append('')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inputs', nargs='+')
    ap.add_argument('--title', default='Generated codebases')
    a = ap.parse_args()
    runs = collect(a.inputs)
    if not runs:
        print('no reports found', file=sys.stderr)
        return 2
    print('## %s\n' % a.title)
    print('\n'.join(summary_table(runs)))
    print('\nLatency in ms, median / 95th percentile (n):\n')
    print('\n'.join(latency_table(runs)))
    f = failures(runs)
    if f:
        print('\n### Failing expectations\n')
        print('\n'.join(f))
    return 0


if __name__ == '__main__':
    sys.exit(main())
