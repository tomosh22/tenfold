#!/usr/bin/env python3
"""Run `tenfold-cli bench` cold and warm, aggregate, and print a Markdown table.

Cold run: <ws>/.tenfold and <ws>/.cache/clangd are deleted first (no persisted
fast index, no generated compile database, no clangd background index).
Warm run: same workspace, caches left in place from the preceding run.

The aggregator is generic: every numeric key under "phases" in the CLI's JSON
is aggregated (median, min), so new phases need no change here. Keys listed in
BUDGETS additionally get a budget column with pass/fail (plan section 1.7).
Standard library only.
"""
import argparse
import json
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

# phase key -> (cold budget, warm budget). A key ending in "[" matches by prefix,
# so symFast[<query>]Ms is covered by "symFast[".
BUDGETS = {
    "startReturnMs": (3000, 1000),   # startup to interactive
    "symFast[": (50, 50),            # symbol search, fast tier
    "peakRssMiB": (500, 500),        # editor process, excluding clangd
}
UNITS = {"peakRssMiB": "MiB"}


def budget_for(key):
    if key in BUDGETS:
        return BUDGETS[key]
    for prefix, b in BUDGETS.items():
        if prefix.endswith("[") and key.startswith(prefix):
            return b
    return (None, None)


def clear_caches(ws):
    for rel in (".tenfold", ".cache/clangd"):
        shutil.rmtree(Path(ws) / rel, ignore_errors=True)


def run_once(cli, ws, extra, timeout):
    cmd = [cli, "bench", str(ws), "--json"] + extra
    t = time.monotonic()
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=timeout)
    if p.returncode != 0:
        sys.exit("bench failed (%d): %s\n%s" % (p.returncode, " ".join(cmd), p.stderr[-2000:]))
    out = p.stdout.strip().splitlines()
    try:
        data = json.loads(out[-1])
    except (IndexError, ValueError):
        sys.exit("bench produced no JSON: %s\n%s" % (" ".join(cmd), p.stdout[-2000:]))
    data["_wallS"] = time.monotonic() - t
    return data


def aggregate(runs):
    """-> ordered {key: (median, min, n)} over numeric phases."""
    keys = []
    for r in runs:
        for k in r.get("phases", {}):
            if k not in keys:
                keys.append(k)
    out = {}
    for k in keys:
        vals = [r["phases"][k] for r in runs if isinstance(r.get("phases", {}).get(k), (int, float))]
        if vals:
            out[k] = (statistics.median(vals), min(vals), len(vals))
    return out


def fmt(v):
    if v is None:
        return "-"
    return "%.1f" % v if v >= 10 else "%.2f" % v


def verdict(v, budget):
    if budget is None or v is None:
        return ""
    return "pass" if v <= budget else "FAIL"


def markdown(ws, cold, warm, meta, counts):
    cagg = aggregate(cold) if cold else {}
    wagg = aggregate(warm) if warm else {}
    keys = list(cagg) + [k for k in wagg if k not in cagg]
    lines = []
    lines.append("Workspace: `%s`  " % ws)
    lines.append("clangd: %s; compile flags: %s; file: `%s`; runs: %d cold, %d warm (median / min)" % (
        meta.get("clangd", "?"), meta.get("compdbSource", "?"), meta.get("file", "?"), len(cold), len(warm)))
    lines.append("")
    if counts:
        lines.append("Counts: " + ", ".join("%s=%d" % (k, v) for k, v in counts.items()
                                            if not k.endswith("Results") and v >= 0))
        lines.append("")
    lines.append("| Phase | Cold median | Cold min | Warm median | Warm min | Budget cold / warm | Cold | Warm |")
    lines.append("|---|---:|---:|---:|---:|---|---|---|")
    for k in keys:
        c = cagg.get(k)
        w = wagg.get(k)
        bc, bw = budget_for(k)
        unit = UNITS.get(k, "ms")
        btxt = "" if bc is None else "%g / %g %s" % (bc, bw, unit)
        lines.append("| `%s` (%s) | %s | %s | %s | %s | %s | %s | %s |" % (
            k, unit, fmt(c[0]) if c else "-", fmt(c[1]) if c else "-", fmt(w[0]) if w else "-",
            fmt(w[1]) if w else "-", btxt, verdict(c[0] if c else None, bc), verdict(w[0] if w else None, bw)))
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("workspace")
    ap.add_argument("--cli", default=str(Path(__file__).resolve().parent.parent / "build-rel" / "tenfold-cli"))
    ap.add_argument("--file", help="<rel path>:<line>:<col> for the def / TU-parsed phases")
    ap.add_argument("--sym", action="append", default=[], help="symbol query (repeatable)")
    ap.add_argument("--runs", type=int, default=3, help="process runs per mode (cold and warm)")
    ap.add_argument("--inner-runs", type=int, default=5, help="repeats inside one process for warm def / sym medians")
    ap.add_argument("--no-clangd", action="store_true")
    ap.add_argument("--wait-index", type=int, default=0, help="ms to wait for clangd's background index before sym")
    ap.add_argument("--timeout", type=int, default=3600, help="per-process timeout, seconds")
    ap.add_argument("--json-out", help="also write all raw run JSON here")
    args = ap.parse_args()

    ws = Path(args.workspace).resolve()
    if not ws.is_dir():
        sys.exit("not a directory: %s" % ws)
    if not Path(args.cli).exists():
        sys.exit("CLI not found: %s (build Release first)" % args.cli)
    extra = ["--runs", str(args.inner_runs)]
    if args.file:
        extra += ["--file", args.file]
    for s in args.sym:
        extra += ["--sym", s]
    if args.no_clangd:
        extra.append("--no-clangd")
    if args.wait_index:
        extra += ["--wait-index", str(args.wait_index)]

    cold, warm = [], []
    for i in range(args.runs):
        clear_caches(ws)
        print("cold run %d/%d ..." % (i + 1, args.runs), file=sys.stderr)
        cold.append(run_once(args.cli, ws, extra, args.timeout))
    # The last cold run left every cache populated, which is what warm means.
    for i in range(args.runs):
        print("warm run %d/%d ..." % (i + 1, args.runs), file=sys.stderr)
        warm.append(run_once(args.cli, ws, extra, args.timeout))

    first = cold[0]
    print(markdown(ws, cold, warm, first.get("meta", {}), first.get("counts", {})))
    if args.json_out:
        Path(args.json_out).write_text(json.dumps({"cold": cold, "warm": warm}, indent=1))


if __name__ == "__main__":
    main()
