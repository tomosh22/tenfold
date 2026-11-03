#!/usr/bin/env python3
"""Run a command and record wall time and the peak resident memory of its process tree (Linux, /proc).

    measure_run.py --out meta.json [--merge gen-stats.json] -- command args...

The output JSON has `wallSec`, `exitCode` and `rssMiB`: the peak of the main process (`main`) and of the biggest `clangd`
process / the sum of all clangd processes at once (`clangd`, `clangdSum`).  `VmHWM` (the kernel's high-water mark) is read
at every sample, so a short peak between two samples is still seen unless the process exits in between.
"""
import argparse
import json
import os
import subprocess
import sys
import time


def status(pid):
    d = {}
    try:
        with open('/proc/%d/status' % pid) as fh:
            for ln in fh:
                k, _, v = ln.partition(':')
                d[k] = v.strip()
    except OSError:
        return None
    return d


def kb(d, key):
    try:
        return int(d.get(key, '0 kB').split()[0])
    except ValueError:
        return 0


def children_map():
    m = {}
    for n in os.listdir('/proc'):
        if n.isdigit():
            st = status(int(n))
            if st:
                m.setdefault(int(st.get('PPid', '0') or 0), []).append((int(n), st.get('Name', '')))
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', required=True)
    ap.add_argument('--merge', help='JSON file whose keys are copied into the output under "gen"')
    ap.add_argument('--interval', type=float, default=0.5)
    ap.add_argument('cmd', nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.cmd[1:] if a.cmd and a.cmd[0] == '--' else a.cmd
    t0 = time.time()
    p = subprocess.Popen(cmd)
    main_hwm = 0
    clangd_hwm = {}
    clangd_sum = 0
    while p.poll() is None:
        cm = children_map()
        todo = [p.pid]
        seen = set()
        cur_sum = 0
        while todo:
            pid = todo.pop()
            if pid in seen:
                continue
            seen.add(pid)
            st = status(pid)
            if st is None:
                continue
            if pid == p.pid:
                main_hwm = max(main_hwm, kb(st, 'VmHWM'))
            elif st.get('Name', '').startswith('clangd'):
                clangd_hwm[pid] = max(clangd_hwm.get(pid, 0), kb(st, 'VmHWM'))
                cur_sum += kb(st, 'VmRSS')
            for c, _ in cm.get(pid, []):
                todo.append(c)
        clangd_sum = max(clangd_sum, cur_sum)
        time.sleep(a.interval)
    out = {'wallSec': round(time.time() - t0, 1), 'exitCode': p.returncode, 'cmd': cmd,
           'rssMiB': {'main': round(main_hwm / 1024.0, 1),
                      'clangd': round(max(clangd_hwm.values() or [0]) / 1024.0, 1),
                      'clangdSum': round(clangd_sum / 1024.0, 1)}}
    if a.merge and os.path.exists(a.merge):
        with open(a.merge) as fh:
            out['gen'] = json.load(fh)
    with open(a.out, 'w') as fh:
        json.dump(out, fh, indent=1, sort_keys=True)
    return p.returncode


if __name__ == '__main__':
    sys.exit(main())
