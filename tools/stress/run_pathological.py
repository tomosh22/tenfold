#!/usr/bin/env python3
"""Driver for the pathological-workspace stress runs (plan W9f). Standard library only.

  run_pathological.py --build build-rel [--scale full|asan|small] [--scenario NAME ...] [--work DIR]
                      [--timeout SEC] [--clangd] [--json-out FILE] [--md-out FILE] [--keep]

For every scenario it generates the workspace (gen_pathological.py), then runs under a hard timeout:
  bench      `tenfold-cli bench` (no clangd): scan, fast index, a def and symbol searches; the CLI reports its own peak RSS
  repl       a scripted `tenfold-cli <ws>` session: open / goto / insert / def / refs / sym / grep on the largest files
  clangd     (--clangd, scenarios where it is meaningful) `tenfold-cli bench` with clangd on
  editor     `tenfold-tests pathological_full` with TENFOLD_PATHOLOGICAL_DIR (open, scroll to end, edit, undo, find,
             replace on the giant files; also run for the ASan binary)
  churn      a REPL session whose workspace is being mutated by gen_pathological.py --churn (file watcher stress)
A step fails on: timeout (hang; the whole process tree is killed), a non-zero exit status or a signal (crash), a
sanitizer report on stderr, or missing expected output. Peak RSS of the process tree is sampled from /proc every 50 ms
and flagged when above 4 GiB or above 10x the on-disk size of the workspace plus a 300 MiB baseline.
The fsodd scenario runs as the unprivileged user `nobody` when started as root (chmod 000 does not stop root).
"""
import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True
import gen_pathological as gen  # noqa: E402

SANITIZER_RE = re.compile(r"ERROR: (Address|Leak|Thread)Sanitizer|runtime error:|SUMMARY: (Address|Undefined|Leak)|AddressSanitizer:DEADLYSIGNAL")
RSS_FLAG_ABS_MIB = 4096
RSS_BASELINE_MIB = 300
RSS_KILL_MIB = 7000  # hard stop: the process tree is killed above this (the machine has ~15 GB)


def dir_size(path):
    total = 0
    for dp, dns, fns in os.walk(path, onerror=lambda e: None):
        for f in fns:
            try:
                total += os.lstat(os.path.join(dp, f)).st_size
            except OSError:
                pass
    return total


def proc_tree(pid):
    """pid and all descendants (via /proc/*/stat ppid)."""
    kids = {}
    for e in os.listdir("/proc"):
        if e.isdigit():
            try:
                with open("/proc/%s/stat" % e) as f:
                    st = f.read()
                ppid = int(st[st.rindex(")") + 2:].split()[1])
                kids.setdefault(ppid, []).append(int(e))
            except (OSError, ValueError):
                pass
    out, todo = [], [pid]
    while todo:
        p = todo.pop()
        out.append(p)
        todo.extend(kids.get(p, []))
    return out


def rss_kib(pid):
    try:
        with open("/proc/%d/status" % pid) as f:
            for l in f:
                if l.startswith("VmRSS:"):
                    return int(l.split()[1])
    except (OSError, ValueError):
        pass
    return 0


class Result:
    def __init__(self, scenario, step):
        self.scenario, self.step = scenario, step
        self.ok = True
        self.status = "ok"
        self.wall = 0.0
        self.rss_mib = 0.0  # sampled: the largest single process (the editor, or clangd)
        self.tree_rss_mib = 0.0  # sampled: sum over the process tree
        self.self_rss_mib = None  # reported by the CLI (editor process only)
        self.notes = []
        self.metrics = {}
        self.sanitizer = []

    def fail(self, why):
        self.ok = False
        if self.status == "ok":
            self.status = why
        self.notes.append(why)

    def asdict(self):
        return self.__dict__


def run(cmd, res, timeout, stdin_text=None, env=None, cwd=None, stdin_feeder=None, as_user=None):
    """Run cmd; fills res (wall, rss, status). Returns (returncode, stdout, stderr)."""
    if as_user:
        cmd = ["setpriv", "--reuid=%d" % as_user, "--regid=%d" % as_user, "--clear-groups"] + cmd
    e = dict(os.environ)
    e.update(env or {})
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=e, cwd=cwd,
                         start_new_session=True)
    peak = [0, 0]  # [largest single process, whole tree] in KiB
    stop = threading.Event()
    killed_for_rss = threading.Event()

    def sample():
        while not stop.is_set():
            per = [rss_kib(x) for x in proc_tree(p.pid)]
            tot = sum(per)
            peak[0] = max(peak[0], max(per))
            peak[1] = max(peak[1], tot)
            if tot / 1024.0 > RSS_KILL_MIB and not killed_for_rss.is_set():  # protect the machine; reported as a failure
                killed_for_rss.set()
                for x in proc_tree(p.pid):
                    try:
                        os.kill(x, signal.SIGKILL)
                    except OSError:
                        pass
            stop.wait(0.05)

    th = threading.Thread(target=sample, daemon=True)
    th.start()
    out_buf, err_buf = [], []

    def drain(stream, buf):
        for chunk in iter(lambda: stream.read1(65536), b""):
            buf.append(chunk)

    ths = [threading.Thread(target=drain, args=(p.stdout, out_buf), daemon=True),
           threading.Thread(target=drain, args=(p.stderr, err_buf), daemon=True)]
    for t in ths:
        t.start()
    if stdin_feeder:
        threading.Thread(target=stdin_feeder, args=(p,), daemon=True).start()
    elif stdin_text is not None:
        try:
            p.stdin.write(stdin_text.encode())
            p.stdin.close()
        except OSError:
            pass
    else:
        p.stdin.close()
    timed_out = False
    try:
        p.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        for pid in proc_tree(p.pid):  # tenfold + clangd + children, never leave anything behind
            try:
                os.kill(pid, signal.SIGKILL)
            except OSError:
                pass
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except OSError:
            pass
        p.wait()
    # a crashed parent can leave clangd behind
    try:
        os.killpg(p.pid, signal.SIGKILL)
    except OSError:
        pass
    stop.set()
    th.join()
    for t in ths:
        t.join(timeout=5)
    res.wall = time.monotonic() - t0
    res.rss_mib = peak[0] / 1024.0  # a forked child shows its parent's pages until exec, so the tree sum overcounts
    res.tree_rss_mib = peak[1] / 1024.0
    out = b"".join(out_buf).decode("utf-8", "replace")
    err = b"".join(err_buf).decode("utf-8", "replace")
    rc = p.returncode
    if killed_for_rss.is_set():
        res.fail("RSS > %d MiB (killed)" % RSS_KILL_MIB)
    elif timed_out:
        res.fail("HANG (killed after %ds)" % timeout)
    elif rc < 0:
        res.fail("CRASH (signal %s)" % signal.Signals(-rc).name)
    elif rc != 0:
        res.fail("exit status %d" % rc)
    for l in err.splitlines():
        if SANITIZER_RE.search(l):
            res.sanitizer.append(l.strip()[:300])
    if res.sanitizer:
        res.fail("SANITIZER report")
    return rc, out, err


def check_rss(res, disk_bytes):
    if res.tree_rss_mib > RSS_FLAG_ABS_MIB:
        res.fail("RSS %.0f MiB (process tree) > 4 GiB" % res.tree_rss_mib)
    elif res.rss_mib > RSS_BASELINE_MIB + 10 * disk_bytes / 1048576.0:
        res.fail("RSS %.0f MiB > 10x on-disk size (%.0f MiB)" % (res.rss_mib, disk_bytes / 1048576.0))


# ------------------------------------------------------------------------------------------------------------------
# per-scenario plans
# ------------------------------------------------------------------------------------------------------------------
PLANS = {
    "tree": dict(
        file="main.cpp:2:5", syms=["flat_fn_77", "Widget", "deep_fn"],
        repl=["wait-fast 600000", "status", "open flat/f2.cpp:1:30", "def", "refs", "sym flat_fn_9", "sym Widget",
              "grep flat_fn_1", "grep zzz_not_there", "open deep/level0/level1/deep1.cpp:1:5", "def", "quit"],
        clangd=False),
    "bigfiles": dict(
        file="src/million_lines.cpp:%LAST%:30", syms=["giant_fn_999", "after_long_line", "minified_main"],
        repl=["wait-fast 900000", "status", "open src/million_lines.cpp:%LAST%:30", "def", "goto 500000 3", "insert x\\n",
              "def", "refs", "sym giant_tail", "grep needle_4999", "grep giant_tail_marker", "open src/oneline_long.cpp:1:500000",
              "def", "insert y", "refs", "open src/oneline_100k.cpp:1:100", "def", "open src/minified.cpp:1:5000",
              "def", "insert z", "refs", "sym minified", "grep needle_1", "quit"],
        clangd=False, editor=True),
    "nesting": dict(
        file="src/braces.cpp:1:12", syms=["deepest_ns_fn", "Ident", "var_77", "alt_macro_use"],
        repl=["wait-fast 600000", "status", "open src/braces.cpp:1:12", "def", "refs", "open src/long_ident.cpp:2:1000",
              "def", "refs", "hover", "sym Ident", "sym var_999", "sym nested_fn", "grep after_", "open src/many_decls.cpp:5:5",
              "def", "refs", "open src/macro_alts.cpp:%AFTERMACRO%", "def", "refs", "open src/templates.cpp:2:20", "def",
              "quit"],
        clangd=False, editor=True),
    "encodings": dict(
        file="main.cpp:2:30", syms=["enc_marker", "after_nul", "wide_marker"],
        repl=["wait-fast 600000", "status", "open main.cpp:2:30", "def", "refs", "open enc/latin1.cpp:1:5", "def",
              "insert é", "open enc/random_binary.cpp:1:50", "def", "refs", "open enc/nul_bytes.cpp:1:5", "def",
              "open enc/cr_only.cpp:2:1", "def", "open enc/utf16le_bom.cpp:1:5", "def", "open enc/all_nul.cpp:1:1",
              "insert q", "sym enc_marker", "sym after_nul", "grep enc_marker", "grep caf", "quit"],
        clangd=True),
    "fsodd": dict(
        file="real/a.cpp:1:6", syms=["fs_real_a", "name_marker_3", "inner_marker_1", "path_max_marker"],
        repl=["wait-fast 600000", "status", "open real/a.cpp:1:6", "def", "refs", "sym fs_real_a", "sym name_marker",
              "sym path_max_marker", "grep fs_real_a", "grep outside_secret", "open noread.cpp:1:5", "open locked/inside.cpp:1:1",
              "open broken.cpp:1:1", "open link_devzero.cpp:1:1", "open link_devnull.cpp:1:1", "open fifo.cpp:1:1",
              "open 'names/spaces in name.cpp:1:5'", "open names/café.cpp:1:5", "quit"],
        clangd=False, as_nobody=True),
    "truncated": dict(
        file="trunc/sym_then_mid_string.cpp:1:6", syms=["before_cut_marker", "BeforeCut", "trunc_main"],
        repl=["wait-fast 600000", "status", "open trunc/sym_then_mid_string.cpp:1:6", "def", "refs", "sym before_cut",
              "open trunc/end_mid_raw_string.cpp:1:6", "def", "insert x", "open trunc/end_mid_block_comment.cpp:2:1",
              "open trunc/cut_3.cpp:1:1", "sym BeforeCut", "grep before_cut", "quit"],
        clangd=True),
    "mixed": dict(
        file="src/main.cpp:5:24", syms=["Shape", "area"],
        repl=["wait-fast 600000", "status", "open src/main.cpp:5:24", "def", "refs", "sym Shape", "sym area", "grep area", "quit"],
        clangd=True, expect_def="shape.h"),
}


def last_line(path):
    n = 0
    with open(path, "rb") as f:
        for _ in f:
            n += 1
    return n


def subst(s, ws):
    if "%LAST%" in s:
        s = s.replace("%LAST%", str(last_line(ws / "src" / "million_lines.cpp")))
    if "%AFTERMACRO%" in s:
        s = s.replace("%AFTERMACRO%", "4:30")
    return s


def step_bench(cli, ws, plan, res, timeout, clangd, as_user=None, extra=()):
    cmd = [cli, "bench", str(ws), "--json", "--runs", "2"]
    if not clangd:
        cmd.append("--no-clangd")
    else:
        cmd += ["--wait-index", "20000"]
    if plan.get("file"):
        cmd += ["--file", subst(plan["file"], ws)]
    for s in plan["syms"]:
        cmd += ["--sym", s]
    cmd += list(extra)
    rc, out, err = run(cmd, res, timeout, as_user=as_user, env=asan_env())
    if res.ok:
        try:
            j = json.loads(out.strip().splitlines()[-1])
            res.metrics = {k: round(v, 2) for k, v in j.get("phases", {}).items() if isinstance(v, (int, float))}
            res.metrics.update({"count." + k: v for k, v in j.get("counts", {}).items() if k in ("sourceFiles", "headerFiles", "fastFiles", "fastSymbols")})
            res.self_rss_mib = j.get("phases", {}).get("peakRssMiB")
        except (ValueError, IndexError):
            res.fail("no JSON from bench")
    return err


def asan_env():
    return {"ASAN_OPTIONS": "detect_leaks=1:abort_on_error=0:detect_stack_use_after_return=0",
            "UBSAN_OPTIONS": "print_stacktrace=1:halt_on_error=1"}


def step_repl(cli, ws, plan, res, timeout, clangd, as_user=None):
    cmds = [subst(c, ws) for c in plan["repl"]]
    cmd = [cli, str(ws), "--quiet"] + ([] if clangd else ["--no-clangd"])
    rc, out, err = run(cmd, res, timeout, stdin_text="\n".join(cmds) + "\n", as_user=as_user, env=asan_env())
    if res.ok and "> quit" not in out:
        res.fail("REPL did not reach quit")
    res.metrics["repl_commands"] = len(cmds)
    if "timed out" in out:
        res.notes.append("a query timed out (30 s)")
        res.fail("query timeout")
    if plan.get("expect_def") and plan["expect_def"] not in out:
        res.fail("expected definition %s missing" % plan["expect_def"])
    return out


def step_editor(tests_bin, scen_root, res, timeout, scale):
    cmd = [tests_bin, "pathological_full"]
    env = dict(asan_env())
    env["TENFOLD_PATHOLOGICAL_DIR"] = str(scen_root)
    env["TENFOLD_PATHOLOGICAL_SCALE"] = scale
    rc, out, err = run(cmd, res, timeout, env=env)
    for l in err.splitlines():
        if "PERF:" in l or "STRESS:" in l:
            res.notes.append(l.strip()[:200])
    if res.ok and "0 failed" not in err:
        res.fail("editor tests failed")
    res.metrics["editor_tests"] = len([l for l in err.splitlines() if "[  OK  ]" in l])
    return err


def step_churn(cli, ws, res, timeout, seconds, seed, clangd, as_user=None):
    """REPL stays alive and keeps querying while a mutator process hammers the workspace."""
    mut = {}

    def mutate():
        r = subprocess.run([sys.executable, str(HERE / "gen_pathological.py"), "--churn", str(ws), "--seconds", str(seconds), "--seed", str(seed)],
                           capture_output=True, text=True)
        mut["out"] = r.stdout.strip()
        mut["rc"] = r.returncode

    def feeder(p):
        try:
            w = lambda s: (p.stdin.write((s + "\n").encode()), p.stdin.flush())
            w("wait-fast 600000")
            w("open src/main.cpp:1:5")
            t = threading.Thread(target=mutate)
            t.start()
            end = time.monotonic() + seconds
            i = 0
            while time.monotonic() < end:
                for c in ("grep churn_", "sym churn", "status", "open src/main.cpp:1:5", "def", "insert a", "grep appended"):
                    w(c)
                i += 1
                time.sleep(0.05)
            t.join()
            time.sleep(1.0)
            w("grep churn_")
            w("sym churn")
            w("quit")
            p.stdin.close()
        except (OSError, ValueError):
            pass

    cmd = [cli, str(ws), "--quiet"] + ([] if clangd else ["--no-clangd"])
    rc, out, err = run(cmd, res, timeout, stdin_feeder=feeder, as_user=as_user, env=asan_env())
    if res.ok and "> quit" not in out:
        res.fail("REPL did not reach quit")
    res.metrics["churn_ops"] = mut.get("out", "")
    return out


def make_churn_ws(work):
    ws = work / "churn_ws"
    if ws.exists():
        shutil.rmtree(ws)
    (ws / "src").mkdir(parents=True)
    (ws / "src" / "main.cpp").write_text("int main() { return 0; }\n")
    for i in range(300):
        (ws / "src" / f"s{i}.cpp").write_text(f"int s{i}() {{ return {i}; }}\n")
    return ws


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default="build-rel", help="build dir with tenfold-cli (and tenfold-tests)")
    ap.add_argument("--scale", choices=list(gen.SCALE_INDEX), default="asan")
    ap.add_argument("--scenario", action="append")
    ap.add_argument("--work", default=None, help="scratch directory (default: a fresh dir under $TMPDIR)")
    ap.add_argument("--timeout", type=int, default=600, help="hard timeout per step, seconds")
    ap.add_argument("--clangd", action="store_true", help="also run the clangd step where meaningful")
    ap.add_argument("--no-editor", action="store_true", help="skip the tenfold-tests editor step")
    ap.add_argument("--churn-seconds", type=float, default=8.0)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--knob", action="append")
    ap.add_argument("--json-out")
    ap.add_argument("--md-out")
    ap.add_argument("--keep", action="store_true", help="keep generated data")
    a = ap.parse_args()

    build = Path(a.build).resolve()
    cli = str(build / "tenfold-cli")
    tests = str(build / "tenfold-tests")
    if not os.access(cli, os.X_OK):
        sys.exit("no %s; build it first" % cli)
    work = Path(a.work) if a.work else Path(os.environ.get("TMPDIR", "/tmp")) / ("w9f_stress_%d" % os.getpid())
    work.mkdir(parents=True, exist_ok=True)
    names = a.scenario or list(PLANS) + ["churn"]
    results = []
    root_user = os.geteuid() == 0
    for name in names:
        if name == "churn":
            ws = make_churn_ws(work)
            r = Result("churn", "churn")
            step_churn(cli, ws, r, a.timeout, a.churn_seconds, a.seed, False)
            check_rss(r, dir_size(ws))
            results.append(r)
            print(fmt_line(r), flush=True)
            if not a.keep:
                shutil.rmtree(ws, ignore_errors=True)
            continue
        plan = PLANS[name]
        t0 = time.monotonic()
        ws = gen.make_scenario(work, name, a.scale, a.seed, a.knob)
        scen_root = work / name
        gen_s = time.monotonic() - t0
        disk = dir_size(scen_root)
        user = None
        if plan.get("as_nobody") and root_user:
            user = 65534
            subprocess.run(["chown", "-R", "nobody:nogroup", str(scen_root)], check=False)
            # the parents of `work` must be traversable by nobody
        print("== %s: generated %.0f MiB in %.1fs at %s" % (name, disk / 1048576.0, gen_s, ws), flush=True)
        steps = [("bench", lambda r: step_bench(cli, ws, plan, r, a.timeout, False, user)),
                 ("repl", lambda r: step_repl(cli, ws, plan, r, a.timeout, False, user))]
        if a.clangd and plan.get("clangd"):
            steps.append(("clangd", lambda r: step_bench(cli, ws, plan, r, a.timeout, True, user)))
            steps.append(("repl+clangd", lambda r: step_repl(cli, ws, plan, r, a.timeout, True, user)))
        if plan.get("editor") and not a.no_editor and os.access(tests, os.X_OK):
            steps.append(("editor", lambda r: step_editor(tests, scen_root, r, a.timeout, a.scale)))
        for sname, fn in steps:
            r = Result(name, sname)
            r.metrics["disk_mib"] = round(disk / 1048576.0, 1)
            fn(r)
            check_rss(r, disk)
            results.append(r)
            print(fmt_line(r), flush=True)
        if not a.keep:
            for dp, dns, fns in os.walk(scen_root):
                for d in dns:
                    try:
                        os.chmod(os.path.join(dp, d), 0o755)
                    except OSError:
                        pass
            shutil.rmtree(scen_root, ignore_errors=True)
    if a.json_out:
        Path(a.json_out).write_text(json.dumps([r.asdict() for r in results], indent=1))
    md = markdown(results, a)
    if a.md_out:
        Path(a.md_out).write_text(md)
    print(md)
    if not a.keep:
        shutil.rmtree(work, ignore_errors=True)
    return 0 if all(r.ok for r in results) else 1


def fmt_line(r):
    return "%-10s %-12s %-5s wall %7.1fs  rss %7.0f MiB  %s" % (r.scenario, r.step, "PASS" if r.ok else "FAIL", r.wall, r.rss_mib, "; ".join(r.notes)[:200])


def markdown(results, a):
    lines = ["| scenario | step | result | wall s | peak RSS MiB (largest process) | CLI-reported RSS MiB | key timings (ms) | notes |",
             "|---|---|---|---|---|---|---|---|"]
    keys = ("scanMs", "fastIndexMs", "startReturnMs", "defFirstMs", "tuParsedMs")
    for r in results:
        km = ", ".join("%s %s" % (k.replace("Ms", ""), r.metrics[k]) for k in keys if k in r.metrics)
        sf = [k for k in r.metrics if k.startswith("symFast[")]
        if sf:
            km += ", symFast max %.2f" % max(r.metrics[k] for k in sf)
        lines.append("| %s | %s | %s | %.1f | %.0f | %s | %s | %s |" % (
            r.scenario, r.step, "pass" if r.ok else "**FAIL** " + r.status, r.wall, r.rss_mib,
            "-" if r.self_rss_mib is None else "%.0f" % r.self_rss_mib, km, "; ".join(r.sanitizer[:2] or r.notes)[:160].replace("|", "/")))
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    sys.exit(main())
