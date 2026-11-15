#!/usr/bin/env python3
"""Summarise the *.jsonl files of tools/stress/run_editor_stress.sh as a Markdown table (W9i)."""
import glob
import json
import os
import sys


def main(out):
    rows, vals, summ = [], [], []
    for f in sorted(glob.glob(os.path.join(out, '*.jsonl'))):
        for line in open(f):
            line = line.strip()
            if not line.startswith('{'):
                continue
            j = json.loads(line)
            if 'summary' in j:
                summ.append(j)
            elif 'metric' in j:
                rows.append(j)
            else:
                vals.append(j)
    print('| scenario | metric | n | p50 ms | p99 ms | max ms | budget |')
    print('|---|---|---:|---:|---:|---:|---:|')
    for j in rows:
        b = ('%g' % j['budget']) if j.get('budget') else ''
        print('| %s | %s | %d | %.2f | %.2f | %.2f | %s |' % (j['scenario'], j['metric'], j['n'], j['p50'], j['p99'], j['max'], b))
    print()
    print('| scenario | seconds | peak RSS MiB (editor) | end RSS MiB | threads | clangd peak RSS MiB | clangd processes | load before / after |')
    print('|---|---:|---:|---:|---:|---:|---:|---|')
    for j in summ:
        print('| %s | %.1f | %.0f | %.0f | %d | %.0f | %d | %.1f / %.1f |' % (
            j['scenario'], j['seconds'], j['peakRssMiB'], j['endRssMiB'], j['threads'], j['clangdPeakRssMiB'],
            j['clangdProcessesMax'], j['load0'], j['load1']))
    print()
    print('| scenario | value | v |')
    print('|---|---|---:|')
    for j in vals:
        print('| %s | %s | %g %s |' % (j['scenario'], j['value'], j['v'], j.get('unit', '')))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else '.')
