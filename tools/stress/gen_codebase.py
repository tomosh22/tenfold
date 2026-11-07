#!/usr/bin/env python3
"""Deterministic generator of large C/C++ projects with known answers (W9g).

    gen_codebase.py OUT_DIR --profile game|service|lib|c|mixed --size small|medium|large|<files>
                    [--seed N] [--variant cmake|vs] [--samples N] [--stats FILE] [--check]

Writes a project under OUT_DIR in the corpus format of docs/TESTING.md: sources with `/*@name*/` anchors, a
`tenfold-expect.txt`, and either a `compile_commands.json` (`--variant cmake`, uses `@CORPUS_ROOT@`) or a Visual Studio
`.sln` + one `.vcxproj` per module (`--variant vs`).  Everything is standard-library free: `mstd/mstd.h` is the
generator's own mini std substitute.

The generator knows every definition and every use it writes.  Each place where a declared name is written goes through
`Gen.tok(entity, role)`, which leaves a marker in the text; after generation a deterministic sample of entities per
category is "tracked" and only their markers become anchors, so the expectations (def / decl / refs / impl / hover / sym /
typedef / rename / fast / edit) are computed from the generator's own symbol table, not from any tool's output.

Same seed, profile, size, variant and samples give byte-identical output (a private splitmix64 generator is used, never
`random`, `hash()` or set iteration order).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

SIZES = {'small': 300, 'medium': 3000, 'large': 20000}
M64 = (1 << 64) - 1
USE_CAP = 40          # anchored uses per entity; more makes the refs expectation `>=`
INDENT = '    '


class Rng:
    def __init__(s, seed):
        s.x = seed & M64

    def next(s):
        s.x = (s.x + 0x9E3779B97F4A7C15) & M64
        z = s.x
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & M64
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & M64
        return z ^ (z >> 31)

    def below(s, n):
        return s.next() % n if n > 0 else 0

    def rand(s):
        return (s.next() >> 11) / float(1 << 53)

    def chance(s, p):
        return s.rand() < p

    def rng(s, a, b):
        return a + s.below(b - a + 1)

    def choice(s, seq):
        return seq[s.below(len(seq))]

    def wchoice(s, items):
        """items: list of (value, weight)."""
        tot = sum(w for _, w in items)
        x = s.below(tot)
        for v, w in items:
            if x < w:
                return v
            x -= w
        return items[-1][0]

    def shuffle(s, lst):
        for i in range(len(lst) - 1, 0, -1):
            j = s.below(i + 1)
            lst[i], lst[j] = lst[j], lst[i]
        return lst

    def sample(s, seq, k):
        c = list(seq)
        s.shuffle(c)
        return c[:k]


# --------------------------------------------------------------------------------------------------------------------
# vocabulary
ADJ = ['Fast', 'Static', 'Dynamic', 'Shared', 'Local', 'Remote', 'Active', 'Cached', 'Lazy', 'Packed', 'Sparse', 'Linear',
       'Radial', 'Atomic', 'Pooled', 'Scoped', 'Primary', 'Basic', 'Smooth', 'Rigid', 'Sorted', 'Hashed', 'Mapped',
       'Queued', 'Dirty', 'Frozen', 'Nested', 'Merged', 'Split', 'Rolling', 'Stable', 'Sampled', 'Layered', 'Masked',
       'Tiled', 'Binned', 'Cyclic', 'Stacked', 'Weighted', 'Bounded']
NOUNS = {
    'game': ['Mesh', 'Shader', 'Texture', 'Camera', 'Light', 'Sprite', 'Body', 'Collider', 'Joint', 'Mixer', 'Emitter',
             'Entity', 'Component', 'Scene', 'Prefab', 'Animator', 'Skeleton', 'Clip', 'Agent', 'Path', 'Inventory',
             'Quest', 'Widget', 'Panel', 'Button', 'Packet', 'Channel', 'Script', 'Binding', 'Vector', 'Matrix',
             'Quat', 'Ray', 'Frustum', 'Bounds', 'Arena', 'Pool', 'Buffer', 'Frame', 'Batch', 'Material', 'Bitmap',
             'Font', 'Glyph', 'Cursor', 'Trigger', 'Spawner', 'Tween', 'Curve', 'Terrain'],
    'service': ['Account', 'Order', 'Invoice', 'Customer', 'Payment', 'Ledger', 'Tariff', 'Session', 'Token', 'Policy',
                'Rule', 'Report', 'Schedule', 'Notice', 'Mailbox', 'Quota', 'Tenant', 'Catalog', 'Product', 'Cart',
                'Shipment', 'Address', 'Profile', 'Audit', 'Event', 'Job', 'Task', 'Queue', 'Worker', 'Gateway',
                'Endpoint', 'Route', 'Handler', 'Validator', 'Mapper', 'Repository', 'Index', 'Ticket', 'Contract',
                'Balance'],
    'lib': ['Range', 'View', 'Iterator', 'Allocator', 'Tuple', 'Variant', 'Optional', 'Span', 'Slice', 'Matrix',
            'Polynomial', 'Interval', 'Fraction', 'Graph', 'Tree', 'Heap', 'Ring', 'Table', 'Codec', 'Parser',
            'Lexer', 'Visitor', 'Functor', 'Adapter', 'Trait', 'Kernel', 'Filter', 'Solver', 'Stream', 'Pipe',
            'Delegate', 'Signal', 'Slot', 'Handle', 'Pointer', 'Array', 'Grid', 'Lattice', 'Sequence', 'Bundle'],
    'c': ['Buffer', 'Packet', 'Socket', 'Timer', 'Queue', 'Stack', 'List', 'Table', 'Parser', 'Lexer', 'Token', 'Block',
          'Pool', 'Arena', 'Frame', 'Window', 'Event', 'Device', 'Driver', 'Channel', 'Stream', 'Record', 'Entry',
          'Slot', 'Page', 'Cache', 'Index', 'Journal', 'Mutex', 'Signal', 'Region', 'Vector', 'Matrix', 'Image'],
}
VERBS = ['compute', 'update', 'apply', 'build', 'load', 'save', 'find', 'merge', 'split', 'reset', 'flush', 'visit',
         'scan', 'probe', 'resolve', 'encode', 'decode', 'bind', 'mark', 'sweep', 'count', 'fetch', 'store', 'select',
         'filter', 'sort', 'shift', 'scale', 'rotate', 'combine', 'measure', 'collect', 'derive', 'verify']
COMMON_METHODS = ['get', 'set', 'size', 'update', 'reset', 'init', 'count', 'name', 'value', 'run', 'process', 'next',
                  'clear', 'empty', 'data', 'id', 'apply', 'make', 'find', 'load', 'save', 'add', 'push', 'pop', 'top',
                  'flush']
COMMON_CLASSES = ['Node', 'Impl', 'Item', 'Config', 'Entry', 'Handle', 'Context', 'State', 'Manager', 'Buffer', 'Cache',
                  'Policy', 'Options', 'Result', 'Info', 'Base', 'Util', 'Helper', 'Registry', 'Provider']
COMMON_STATIC = ['helper', 'clamp', 'scale', 'mix', 'sum', 'total', 'pick', 'step', 'fold', 'norm']
COMMON_ENUMERATORS = ['None', 'Count', 'First', 'Last', 'Default', 'Red', 'Green', 'Blue', 'Unknown', 'Auto', 'Max']
FIELD_WORDS = ['count', 'weight', 'level', 'limit', 'offset', 'scale', 'stamp', 'flags', 'index', 'total', 'width',
               'height', 'depth', 'ratio', 'score', 'rank', 'phase', 'delta', 'mass', 'speed']
MOD_WORDS = {
    'game': ['math', 'core', 'memory', 'container', 'io', 'asset', 'render', 'shader', 'physics', 'collision', 'audio',
             'input', 'entity', 'scene', 'anim', 'ai', 'gameplay', 'ui', 'net', 'script', 'tools', 'save', 'camera',
             'particles', 'terrain', 'dialog', 'inventory', 'quest', 'combat', 'effects'],
    'service': ['util', 'config', 'logging', 'domain', 'model', 'repo', 'storage', 'cache', 'auth', 'billing', 'orders',
                'catalog', 'shipping', 'notify', 'mail', 'report', 'audit', 'schedule', 'search', 'gateway', 'api',
                'rpc', 'jobs', 'tenant', 'quota', 'payment', 'ledger', 'admin', 'metrics', 'web'],
    'lib': ['base', 'meta', 'traits', 'alloc', 'range', 'algo', 'numeric', 'linalg', 'poly', 'graph', 'tree', 'codec',
            'parse', 'stream', 'functional', 'tuple', 'variant', 'span', 'units', 'geometry', 'sparse', 'solver',
            'signal', 'filter', 'tensor', 'strings', 'format', 'ranges', 'concepts', 'bits'],
    'c': ['base', 'mem', 'list', 'hash', 'buf', 'str', 'io', 'net', 'sock', 'event', 'timer', 'sched', 'fs', 'proc',
          'log', 'conf', 'parse', 'lex', 'crypt', 'zip', 'db', 'pool', 'tty', 'sig', 'dev', 'rpc', 'vm', 'jit', 'gfx',
          'font'],
}
MOD_SUFFIX = ['x', 'util', 'ext', 'base', 'impl', 'ng', 'lite', 'plus', 'core', 'kit']
TYPES_CXX = [('int', '1'), ('long', '2L'), ('double', '3.5'), ('bool', 'true'), ('unsigned', '7u')]
TYPES_C = [('int', '1'), ('long', '2L'), ('double', '3.5'), ('unsigned', '7u')]
LIT = dict(TYPES_CXX)

# weights of the unit recipes per language
W_CXX = {
    'game': dict(cls=42, funcs=18, tmpl=8, enum=8, macro=5, table=4, chain=2),
    'service': dict(cls=44, funcs=26, tmpl=3, enum=10, macro=6, table=2, chain=3),
    'lib': dict(cls=14, funcs=20, tmpl=46, enum=6, macro=4, table=2, chain=2),
    'c': dict(cls=0, funcs=0, tmpl=0, enum=0, macro=0, table=0, chain=0),
}
W_C = dict(cstruct=40, cfuncs=28, cenum=14, cmacro=8, ctable=6, cchain=4)
PROFILE_KIND = {'game': 'game', 'service': 'service', 'lib': 'lib', 'c': 'c', 'mixed': 'service'}
HIER = {'game': .55, 'service': .6, 'lib': .2, 'c': 0, 'mixed': .45}
NESTED = {'game': .05, 'service': .2, 'lib': .05, 'c': 0, 'mixed': .1}
COMMON_P = {'game': .35, 'service': .4, 'lib': .3, 'c': .3, 'mixed': .35}

# --------------------------------------------------------------------------------------------------------------------
MARK = re.compile('\x01(\\d+):([a-z]+)\x02')


class Ent:
    """Anything with a name that the generator declares: the ground truth of the expectations."""

    def __init__(s, g, kind, name, mod=None, ns='', **kw):
        s.id = g.nid
        g.nid += 1
        s.kind = kind
        s.name = name
        s.mod = mod
        s.ns = ns
        s.owner = None
        s.tracked = False
        s.primary = False
        s.cat = None
        s.anchors = {}
        s.counts = {}
        s.uselangs = set()
        s.capped = False
        s.hdr = None
        s.doc = None
        s.__dict__.update(kw)
        g.ents.append(s)
        if kw.get('census', True):
            g.census[name] = g.census.get(name, 0) + 1
            g.census_ci[name.lower()] = g.census_ci.get(name.lower(), 0) + 1


class FB:
    """A file being built."""

    def __init__(s, path, header, cguard=False, pragma=True):
        s.path = path
        s.header = header
        s.cguard = cguard
        s.pragma = pragma
        s.incs = []
        s.lines = []
        s.first = None

    def inc(s, h):
        if h not in s.incs and h != s.first:
            s.incs.append(h)

    def add(s, *ls):
        s.lines.extend(ls)

    def text(s):
        out = []
        guard = re.sub('[^A-Z0-9]', '_', s.path.upper()) + '_'
        if s.header:
            if s.pragma:
                out.append('#pragma once')
            else:
                out += ['#ifndef ' + guard, '#define ' + guard]
        incs = ([s.first] if s.first else []) + s.incs
        for h in incs:
            out.append('#include "%s"' % h)
        if incs:
            out.append('')
        if s.cguard:
            out += ['#ifdef __cplusplus', 'extern "C" {', '#endif', '']
        out += s.lines
        if s.cguard:
            out += ['', '#ifdef __cplusplus', '}', '#endif']
        if s.header and not s.pragma:
            out += ['', '#endif  // ' + guard]
        return '\n'.join(out) + '\n'


class Mod:
    def __init__(s, idx, name, lang):
        s.idx = idx
        s.name = name
        s.lang = lang
        s.deps = []
        s.ns = ''
        s.prefix = ''
        s.api = {k: [] for k in ('funcs', 'classes', 'poly', 'derived', 'consts', 'enums', 'aliases', 'macros', 'fmacros',
                                 'tmplfuncs', 'tmplclasses', 'statics', 'cmethods', 'cstructs', 'chains')}
        s.files = []
        s.nfiles = 0
        s.budget = 0
        s.units = 0
        s.fnames = set()
        s.cnames = set()
        s.style = 0
        s.snake = False
        s.hdrs = []
        s.srcs = []
        s.closure = []
        s.umbrella = None


def lit(t):
    return LIT[t]


class Gen:
    def __init__(s, profile, size, seed, variant, samples):
        s.profile = profile
        s.kind = PROFILE_KIND[profile]
        s.size = size
        s.seed = seed
        s.variant = variant
        s.samples = samples
        s.r = Rng(seed * 1000003 + 12345)
        s.nid = 0
        s.ents = []
        s.census = {}
        s.census_ci = {}
        s.files = {}
        s.mods = []
        s.used_names = {}
        s.nuniq = 0
        s.expect = []
        s.reserved = set()
        s.apps = []
        s.counter = 0

    # ---- helpers
    def fresh(s):
        s.counter += 1
        return s.counter

    def file(s, path, header=None, **kw):
        if path in s.files:
            return s.files[path]
        if header is None:
            header = path.endswith('.h')
        f = FB(path, header, **kw)
        s.files[path] = f
        return f

    def tok(s, e, role, text=None):
        return '\x01%d:%s\x02%s' % (e.id, role, e.name if text is None else text)

    def qn(s, e, role='use', unqual_ns=None):
        """Name of a namespace-level or class-level entity as written at a use."""
        parts = ''
        if e.owner is not None:
            parts = s.qn_owner(e.owner) + '::'
        elif e.ns and e.ns != unqual_ns:
            parts = e.ns + '::'
        return parts + s.tok(e, role)

    def qn_owner(s, c):
        base = (c.ns + '::') if c.ns else ''
        return base + s.tok(c, 'use')

    def unique_name(s, base, pool='cls'):
        used = s.used_names.setdefault(pool, set())
        n = base
        k = 1
        while n in used or n in s.reserved:
            k += 1
            n = '%s%d' % (base, k)
        used.add(n)
        return n

    def class_name(s, mod):
        r = s.r
        if r.chance(.25):
            for _ in range(5):
                n = r.choice(COMMON_CLASSES)
                if n not in mod.cnames:
                    mod.cnames.add(n)
                    return n
        nouns = NOUNS[s.kind]
        base = (r.choice(ADJ) if r.chance(.6) else '') + r.choice(nouns)
        if r.chance(.2):
            base += r.choice(nouns)
        n = s.unique_name(base, 'cls')
        while n in mod.cnames:
            n = s.unique_name(base + str(s.fresh()), 'cls')
        mod.cnames.add(n)
        return n

    def func_name(s, mod, avoid=(), common=True, pool='fn'):
        r = s.r
        if common and r.chance(COMMON_P[s.profile]):
            for _ in range(6):
                n = r.choice(COMMON_METHODS)
                if n not in avoid:
                    return n
        v = r.choice(VERBS)
        nn = r.choice(NOUNS[s.kind])
        if mod.snake:
            base = v + '_' + nn.lower()
        else:
            base = v + nn
        if base in avoid or s.used_names.get(pool + mod.name, set()) & {base}:
            base = base + str(s.fresh())
        s.used_names.setdefault(pool + mod.name, set()).add(base)
        return base

    def doc(s, what, e):
        r = s.r
        if not r.chance(.55):
            return None
        e.doc = 'docref%d' % e.id
        return '/** %s %s %s, %s. */' % (r.choice(['Computes', 'Returns', 'Updates', 'Resolves', 'Builds', 'Finds']),
                                         r.choice(['the', 'a', 'every']), what, e.doc)

    def pick_api(s, mod, cat):
        srcs = []
        if mod.api[cat]:
            srcs.append(mod)
        for d in mod.deps:
            if d.api[cat]:
                srcs.append(d)
        if not srcs:
            return None
        r = s.r
        m = mod if (mod in srcs and r.chance(.5)) else r.choice(srcs)
        lst = m.api[cat]
        x = r.rand()
        return lst[int(len(lst) * x * x)]

    # ---- namespace helpers
    def ns_open(s, mod):
        if mod.lang == 'c':
            return [], []
        parts = mod.ns.split('::')
        if mod.style == 0:
            return ['namespace %s {' % mod.ns, ''], ['', '}  // namespace %s' % mod.ns]
        return (['namespace %s {' % p for p in parts] + [''],
                [''] + ['}  // namespace %s' % p for p in reversed(parts)])


# --------------------------------------------------------------------------------------------------------------------
class Body:
    """Generates the statements of one function body; every use of a declared name is written through Gen.tok."""

    def __init__(s, g, f, mod, cls=None, inline=False, free_ns=None, cmode=False, extra=None):
        s.g = g
        s.f = f
        s.mod = mod
        s.cls = cls
        s.inline = inline
        s.free_ns = free_ns          # namespace of the enclosing free function (unqualified calls allowed there)
        s.c = cmode
        s.extra = extra or []        # file-local helper entities that may be called
        s.lines = []
        s.n = 0
        s.is_const = False
        s.is_static = False

    def tmp(s, p='o'):
        s.n += 1
        return '%s%d' % (p, s.n)

    def need(s, e):
        if e.hdr:
            s.f.inc(e.hdr)

    def args(s, e):
        return ', '.join(lit(t) for t in e.params)

    def stmts(s, n):
        g = s.g
        r = g.r
        opts = []
        if s.inline:
            opts = [('self', 10)]
        else:
            if s.cls is not None:
                opts.append(('self', 14))
            if s.extra:
                opts.append(('extra', 8))
            opts += [('call', 28), ('const', 8), ('macro', 6), ('alias', 3)]
            if s.c:
                opts += [('cobj', 22), ('cenum', 6)]
            else:
                opts += [('obj', 22), ('enum', 6), ('tmpl', 8), ('static', 4), ('virt', 8)]
        for _ in range(n):
            for _try in range(6):
                k = r.wchoice(opts)
                if getattr(s, 's_' + k)():
                    break

    # -- statements
    def s_self(s):
        c = s.cls
        g = s.g
        r = g.r
        if c is None:
            return False
        cands = [m for m in c.methods if ((not s.is_const or m.const or m.static) and (not s.is_static or m.static))]
        opts = []
        if c.fields and not s.is_static:
            opts.append('field')
        if cands:
            opts.append('method')
        if c.nested:
            opts.append('nested')
        if c.vecfield and not s.is_static:
            opts.append('vec')
        if c.links and not s.inline and not s.is_static:
            opts.append('link')
        if not opts:
            return False
        k = r.choice(opts)
        if k == 'field':
            f = r.choice(c.fields)
            s.lines.append('r += %s;' % (g.tok(f, 'use') if not r.chance(.3) else 'this->' + g.tok(f, 'use')))
        elif k == 'method':
            m = r.choice(cands)
            s.lines.append('r += %s(%s);' % (g.tok(m, 'use'), s.args(m)))
        elif k == 'nested':
            nt = r.choice(c.nested)
            v = s.tmp('n')
            s.lines.append('%s %s;' % (g.tok(nt, 'use'), v))
            s.lines.append('r += %s.%s;' % (v, g.tok(nt.fields[0], 'use')))
        elif k == 'vec':
            if not s.is_const:
                s.lines.append('%s.push_back(1);' % c.vecfield)
            s.lines.append('r += %s.size();' % c.vecfield)
        else:
            lf = r.choice(c.links)
            o = lf.link
            ms = [m for m in o.methods if not m.pure]
            if not ms:
                return False
            s.need(o)
            m = r.choice(ms)
            s.lines.append('if (%s) r += %s->%s(%s);' % (g.tok(lf, 'use'), g.tok(lf, 'use'), g.tok(m, 'use'), s.args(m)))
        return True

    def s_extra(s):
        e = s.g.r.choice(s.extra)
        s.lines.append('r += %s(%s);' % (s.g.tok(e, 'use'), s.args(e)))
        return True

    def call_name(s, e):
        g = s.g
        if s.free_ns is not None and e.ns == s.free_ns and g.r.chance(.3):
            return g.tok(e, 'use')
        return g.qn(e)

    def s_call(s):
        g = s.g
        e = g.pick_api(s.mod, 'funcs')
        if e is None:
            return False
        s.need(e)
        s.lines.append('r += %s(%s);' % (s.call_name(e), s.args(e)))
        return True

    def type_name(s, c):
        return s.g.qn(c)

    def s_obj(s):
        g = s.g
        r = g.r
        c = g.pick_api(s.mod, 'classes')
        if c is None:
            return False
        s.need(c)
        v = s.tmp()
        s.lines.append('%s %s;' % (s.type_name(c), v))
        first = True
        for _ in range(r.rng(1, 3)):
            opts = [m for m in c.methods if not m.pure]
            if c.fields:
                opts += c.fields
            if not opts:
                break
            m = r.choice(opts)
            vt = g.tok(c, 'var', v) if first else v
            first = False
            if m.kind == 'field':
                s.lines.append('r += %s.%s;' % (vt, g.tok(m, 'use')))
            elif m.static:
                s.lines.append('r += %s::%s(%s);' % (s.type_name(c), g.tok(m, 'use'), s.args(m)))
            else:
                s.lines.append('r += %s.%s(%s);' % (vt, g.tok(m, 'use'), s.args(m)))
        if first:
            s.lines.append('(void)%s;' % g.tok(c, 'var', v))
        return True

    def s_virt(s):
        g = s.g
        r = g.r
        d = g.pick_api(s.mod, 'derived')
        if d is None:
            return False
        chain = []
        c = d
        while c is not None:
            chain.append(c)
            c = c.base
        a = r.choice(chain)
        if not a.vtab:
            return False
        key = r.choice(sorted(a.vtab))
        m = a.vtab[key]
        s.need(d)
        s.need(a)
        v = s.tmp('d')
        w = s.tmp('b')
        s.lines.append('%s %s;' % (s.type_name(d), v))
        if r.chance(.5):
            s.lines.append('%s& %s = %s;' % (s.type_name(a), w, v))
            s.lines.append('r += %s.%s(%s);' % (w, g.tok(m, 'use'), s.args(m)))
        else:
            s.lines.append('%s* %s = &%s;' % (s.type_name(a), w, v))
            s.lines.append('r += %s->%s(%s);' % (w, g.tok(m, 'use'), s.args(m)))
        return True

    def s_const(s):
        g = s.g
        e = g.pick_api(s.mod, 'consts')
        if e is None:
            return False
        s.need(e)
        s.lines.append('r += %s;' % g.qn(e))
        return True

    def s_static(s):
        g = s.g
        e = g.pick_api(s.mod, 'statics')
        if e is None:
            return False
        s.need(e.owner)
        if g.r.chance(.3):
            s.lines.append('%s++;' % g.qn(e))
        else:
            s.lines.append('r += %s;' % g.qn(e))
        return True

    def s_enum(s):
        g = s.g
        en = g.pick_api(s.mod, 'enums')
        if en is None:
            return False
        s.need(en)
        e = g.r.choice(en.enumerators)
        if en.scoped:
            if g.r.chance(.5):
                v = s.tmp('k')
                s.lines.append('%s %s = %s::%s;' % (g.qn(en), v, g.qn(en), g.tok(e, 'use')))
                s.lines.append('r += static_cast<int>(%s);' % v)
            else:
                s.lines.append('r += static_cast<int>(%s::%s);' % (g.qn(en), g.tok(e, 'use')))
        else:
            pre = (en.ns + '::') if en.ns else ''
            s.lines.append('r += %s%s;' % (pre, g.tok(e, 'use')))
        return True

    def s_cenum(s):
        return s.s_enum_c()

    def s_enum_c(s):
        g = s.g
        en = g.pick_api(s.mod, 'enums')
        if en is None:
            return False
        s.need(en)
        e = g.r.choice(en.enumerators)
        if g.r.chance(.4):
            v = s.tmp('k')
            s.lines.append('enum %s %s = %s;' % (g.tok(en, 'use'), v, g.tok(e, 'use')))
            s.lines.append('r += %s;' % v)
        else:
            s.lines.append('r += %s;' % g.tok(e, 'use'))
        return True

    def s_macro(s):
        g = s.g
        if g.r.chance(.5):
            e = g.pick_api(s.mod, 'macros')
            if e is None:
                return False
            s.need(e)
            s.lines.append('r += %s;' % g.tok(e, 'use'))
        else:
            e = g.pick_api(s.mod, 'fmacros')
            if e is None:
                return False
            s.need(e)
            s.lines.append('r += %s(%s);' % (g.tok(e, 'use'), '2'))
        return True

    def s_alias(s):
        g = s.g
        e = g.pick_api(s.mod, 'aliases')
        if e is None:
            return False
        s.need(e)
        v = s.tmp('a')
        if e.ns and not s.c:
            s.lines.append('%s %s = 1;' % (g.qn(e), v))
        else:
            s.lines.append('%s %s = 1;' % (g.tok(e, 'use'), v))
        s.lines.append('r += %s;' % v)
        return True

    def s_tmpl(s):
        g = s.g
        r = g.r
        if r.chance(.5):
            c = g.pick_api(s.mod, 'tmplclasses')
            if c is None:
                return False
            s.need(c)
            v = s.tmp('t')
            spec = None
            if c.spec is not None and r.chance(.4):
                spec = c.spec
                s.lines.append('%s<bool> %s;' % (g.qn(spec), v))
                targ = 'true'
            else:
                s.lines.append('%s<int> %s;' % (g.qn(c), v))
                targ = '3'
            owner = spec if spec is not None else c
            for m in owner.methods:
                if m.name == 'set':
                    s.lines.append('%s.%s(%s);' % (v, g.tok(m, 'use'), targ))
                elif m.name == 'get':
                    s.lines.append('r += %s.%s();' % (v, g.tok(m, 'use')))
            return True
        e = g.pick_api(s.mod, 'tmplfuncs')
        if e is None:
            return False
        s.need(e)
        if r.chance(.5):
            s.lines.append('r += %s<int>(2);' % g.qn(e))
        else:
            s.lines.append('r += %s(2.5);' % g.qn(e))
        return True

    # -- C
    def s_cobj(s):
        g = s.g
        r = g.r
        st = g.pick_api(s.mod, 'cstructs')
        if st is None:
            return False
        s.need(st)
        v = s.tmp()
        s.lines.append('%s %s;' % (g.tok(st, 'use'), v))
        first = True
        for _ in range(r.rng(1, 3)):
            if st.methods and r.chance(.7):
                m = r.choice(st.methods)
                vt = v
                first = False
                a = ', '.join(['&' + vt] + [lit(t) for t in m.params[1:]])
                s.lines.append('r += %s(%s);' % (g.tok(m, 'use'), a))
            elif st.fields:
                f = r.choice(st.fields)
                vt = v
                first = False
                s.lines.append('r += %s.%s;' % (vt, g.tok(f, 'use')))
        if first:
            s.lines.append('(void)%s;' % v)
        return True


# --------------------------------------------------------------------------------------------------------------------
def snake(n):
    return re.sub(r'(?<!^)(?=[A-Z])', '_', n).lower()


def mfile(g, mod, rel, **kw):
    f = g.file(rel, **kw)
    mod.nfiles += 1
    return f


def unit_file(g, mod, base):
    base = base[:40]
    p = 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, base)
    n = base
    while p in g.files:
        n = '%s_%d' % (base, g.fresh())
        p = 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, n)
    return n


def params_text(ps):
    return ', '.join('%s p%d' % (t, i) for i, t in enumerate(ps)) if ps else ''


def rand_sig(g, mod, c=False):
    r = g.r
    types = TYPES_C if c else TYPES_CXX
    ret = r.choice(types)[0]
    cand = [(), ('int',), ('double',), ('int', 'int'), ('long',), ('unsigned',), ('double', 'int'), ('int', 'double')]
    if c:
        cand = [()] + cand[1:2] + cand[2:3] + cand[3:]
    return ret, list(r.choice(cand))


def body_lines(g, body, ret, params, c=False, extra_first=None):
    out = ['double r = 0;']
    for i in range(len(params)):
        if g.r.chance(.5):
            out.append('r += p%d;' % i)
    if extra_first:
        out += extra_first
    out += body.lines
    out.append('return (%s)(r);' % ret if not c else 'return (%s)(r);' % ret)
    return out


def make_body(g, f, mod, n, **kw):
    b = Body(g, f, mod, **kw)
    b.stmts(n)
    return b


# ---- C++ class unit -----------------------------------------------------------------------------------------------
def new_method(g, c, name, ret, params, const, static=False, virtual=False, pure=False, ov=None):
    m = Ent(g, 'method', name, c.mod, '', owner=c, ret=ret, params=params, const=const, static=static, virtual=virtual,
            pure=pure, override_of=ov, overriders=[], inline=False, hdr=c.hdr, cat=None)
    c.methods.append(m)
    c.allnames.add(name)
    if ov is not None:
        ov.overriders.append(m)
    return m


def sigkey(name, params):
    return '%s(%s)' % (name, ','.join(params))


def gen_class_unit(g, mod, iface=False):
    r = g.r
    name = g.class_name(mod)
    fname = unit_file(g, mod, snake(name))
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), pragma=r.chance(.6))
    c = Ent(g, 'class', name, mod, mod.ns, methods=[], fields=[], statics=[], base=None, vtab={}, allnames=set(),
            nested=[], links=[], hasctor=False, vecfield=None, hdr='%s/%s.h' % (mod.name, fname), spec=None, derived=[],
            iface=iface, explicit_special=False)
    mod.hdrs.append(c.hdr)
    base = None
    if not iface and r.chance(HIER[g.profile]):
        base = g.pick_api(mod, 'poly')
    if base is not None:
        c.base = base
        c.vtab = dict(base.vtab)
        c.allnames = set(base.allnames)
        hdr.inc(base.hdr)
        base.derived.append(c)
    plan = []  # (kind, payload)
    for k in sorted(c.vtab):
        m0 = c.vtab[k]
        if m0.pure or r.chance(.4):
            plan.append(('ov', m0))
    nv = 0
    if iface:
        nv = r.rng(2, 4)
    elif base is None and r.chance(.35):
        nv = r.rng(1, 3)
    elif base is not None and r.chance(.3):
        nv = 1
    for _ in range(nv):
        plan.append(('vnew', iface))
    for _ in range(0 if iface else r.rng(2, 5)):
        plan.append(('new', None))
    r.shuffle(plan)
    own_new = []
    for kind, p in plan:
        if kind == 'ov':
            m = new_method(g, c, p.name, p.ret, p.params, p.const, virtual=True, ov=p)
            c.vtab[sigkey(p.name, p.params)] = m
        elif kind == 'vnew':
            nm = g.func_name(mod, avoid=c.allnames)
            ret, ps = rand_sig(g, mod)
            m = new_method(g, c, nm, ret, ps, r.chance(.5), virtual=True, pure=bool(p))
            c.vtab[sigkey(nm, ps)] = m
            own_new.append(m)
        else:
            ov_name = None
            cands = [m for m in own_new if not m.virtual]
            if cands and r.chance(.15):
                ov_name = r.choice(cands)
            if ov_name is not None:
                ret, ps = rand_sig(g, mod)
                if sigkey(ov_name.name, ps) in [sigkey(m.name, m.params) for m in c.methods if m.name == ov_name.name]:
                    continue
                m = new_method(g, c, ov_name.name, ret, ps, ov_name.const)
            else:
                nm = g.func_name(mod, avoid=c.allnames)
                ret, ps = rand_sig(g, mod)
                m = new_method(g, c, nm, ret, ps, r.chance(.5), static=r.chance(.12))
                if m.static:
                    m.const = False
            own_new.append(m)
    for m in c.methods:
        if m.override_of is None:
            m.d = g.doc('value', m)
        m.inline = (not m.pure) and r.chance(.35)
    # fields
    for _ in range(0 if iface else r.rng(0, 3)):
        w = r.choice(FIELD_WORDS)
        if w in c.allnames:
            w = '%s%d' % (w, g.fresh())
        t = r.choice(TYPES_CXX)[0]
        f_ = Ent(g, 'field', w, mod, '', owner=c, ftype=t)
        c.allnames.add(w)
        c.fields.append(f_)
    # statics
    if not iface and r.chance(.25):
        w = 'shared_' + r.choice(FIELD_WORDS)
        if w in c.allnames:
            w += str(g.fresh())
        sv = Ent(g, 'static', w, mod, '', owner=c, ftype='int', hdr=c.hdr)
        c.allnames.add(w)
        c.statics.append(sv)
    # nested type
    if not iface and r.chance(NESTED[g.profile]):
        nn = r.choice(['Impl', 'Node', 'Entry', 'State'])
        nt = Ent(g, 'nested', nn, mod, '', owner=c)
        fw = 'val'
        nf = Ent(g, 'field', fw, mod, '', owner=nt, ftype='int')
        nt.fields = [nf]
        c.nested.append(nt)
        c.allnames.add(nn)
    # forward-declared link to an earlier class of this module
    if not iface and mod.api['classes'] and r.chance(.3):
        other = None
        for _ in range(4):
            cand = r.choice(mod.api['classes'])
            if cand.mod is mod and cand.methods:
                other = cand
                break
        if other is not None:
            lf = Ent(g, 'field', 'link%d' % g.fresh(), mod, '', owner=c, ftype=None, link=other)
            c.links.append(lf)
            c.allnames.add(lf.name)
    if not iface and r.chance(.15):
        c.vecfield = 'items%d' % g.fresh()
        c.allnames.add(c.vecfield)
    # constructors / destructor
    ctors = []
    if not iface and r.chance(.15):
        ctors = [Ent(g, 'ctor', name, mod, '', owner=c), Ent(g, 'ctor', name, mod, '', owner=c)]
        c.explicit_special = True
    dtor = None
    if base is None and c.vtab and r.chance(.5):
        dtor = Ent(g, 'dtor', name, mod, '', owner=c)
        c.explicit_special = True
    # an out-of-line method is needed so that the class has a source file
    if all(m.inline or m.pure for m in c.methods) and not c.statics:
        for m in c.methods:
            if not m.pure:
                m.inline = False
                break
    # ---- header text
    L = hdr.lines
    helper = None
    nso, nsc = g.ns_open(mod)
    L += nso
    for lf in c.links:
        L.append('class %s;' % g.tok(lf.link, 'fwd'))
    if c.links:
        L.append('')
    d = g.doc(name.lower(), c)
    if d:
        L.append(d)
    head = 'class %s' % g.tok(c, 'def')
    if base is not None:
        head += ' : public %s' % g.qn(base)
    L += [head + ' {', 'public:']
    for nt in c.nested:
        L.append(INDENT + 'struct %s { int %s = 0; };' % (g.tok(nt, 'def'), g.tok(nt.fields[0], 'def')))
    if ctors:
        L.append(INDENT + '%s() {}' % g.tok(ctors[0], 'def', name))
        L.append(INDENT + 'explicit %s(int) {}' % g.tok(ctors[1], 'def', name))
    if dtor:
        L.append(INDENT + 'virtual ~%s() {}' % g.tok(dtor, 'def', name))
    for f_ in c.fields:
        L.append(INDENT + '%s %s = 0;' % (f_.ftype, g.tok(f_, 'def')))
    for lf in c.links:
        L.append(INDENT + '%s* %s = nullptr;' % (g.qn(lf.link), g.tok(lf, 'def')))
    if c.vecfield:
        L.append(INDENT + 'mstd::vector<int> %s;' % c.vecfield)
        hdr.inc('mstd.h')
    for sv in c.statics:
        L.append(INDENT + 'static int %s;' % g.tok(sv, 'decl'))
    for m in c.methods:
        if getattr(m, 'd', None):
            L.append(INDENT + m.d)
        sig = '%s%s%s %s(%s)%s%s%s' % ('static ' if m.static else '', 'virtual ' if (m.virtual and m.override_of is None) else '',
                                       m.ret, g.tok(m, 'def' if (m.inline or False) else 'decl'),
                                       params_text(m.params), ' const' if m.const else '',
                                       ' override' if m.override_of is not None else '', ' = 0' if m.pure else '')
        if m.inline:
            b = Body(g, hdr, mod, cls=c, inline=True)
            b.is_const = m.const
            b.is_static = m.static
            b.stmts(r.rng(1, 2))
            L.append(INDENT + sig + ' {')
            for ln in body_lines(g, b, m.ret, m.params):
                L.append(INDENT * 2 + ln)
            L.append(INDENT + '}')
        else:
            L.append(INDENT + sig + ';')
    L += ['};']
    L += nsc
    # ---- source
    if any((not m.inline and not m.pure) for m in c.methods) or c.statics:
        cpp = mfile(g, mod, 'modules/%s/src/%s.cpp' % (mod.name, fname))
        mod.srcs.append(cpp.path)
        cpp.first = c.hdr
        extra = []
        if r.chance(.3):
            pn = r.choice(COMMON_STATIC)
            if pn not in c.allnames and pn not in mod.fnames:
                he = Ent(g, 'sfunc', pn, mod, '', ret='int', params=['int'], hdr=None, defsrc=cpp.path)
                extra.append(he)
                helper = he
        if r.chance(.2):
            pf = mfile(g, mod, 'modules/%s/src/%s_priv.h' % (mod.name, fname))
            pn = g.func_name(mod, avoid=c.allnames | mod.fnames, common=False)
            pe = Ent(g, 'func', pn, mod, mod.ns, ret='int', params=['int'], hdr=None, privf=True)
            mod.fnames.add(pn)
            po, pc = g.ns_open(mod)
            pf.lines += po + ['/** Private helper of %s. */' % name,
                              'inline int %s(int p0) { return p0 * %d; }' % (g.tok(pe, 'def'), r.rng(2, 9))] + pc
            cpp.inc('%s_priv.h' % fname)
            extra.append(pe)
        o, cl = g.ns_open(mod)
        cpp.lines += o
        for sv in c.statics:
            cpp.lines.append('int %s::%s = 0;' % (g.tok(c, 'use'), g.tok(sv, 'def')))
        if helper is not None:
            cpp.lines.append('static int %s(int p0) { return p0 + %d; }' % (g.tok(helper, 'def'), r.rng(1, 9)))
        cpp.lines.append('')
        for m in c.methods:
            if m.inline or m.pure:
                continue
            b = Body(g, cpp, mod, cls=c, extra=extra)
            b.is_const = m.const
            b.is_static = m.static
            b.stmts(r.rng(2, 6))
            cpp.lines.append('%s %s::%s(%s)%s {' % (m.ret, g.tok(c, 'use'), g.tok(m, 'def'), params_text(m.params),
                                                    ' const' if m.const else ''))
            for ln in body_lines(g, b, m.ret, m.params):
                cpp.lines.append(INDENT + ln)
            cpp.lines += ['}', '']
        cpp.lines += cl
    # ---- register
    if not c.abstract_():
        mod.api['classes'].append(c)
        if c.base is not None:
            mod.api['derived'].append(c)
    if c.vtab:
        mod.api['poly'].append(c)
    for sv in c.statics:
        mod.api['statics'].append(sv)
    return c


def c_abstract(c):
    return any(m.pure for m in c.vtab.values())


Ent.abstract_ = c_abstract


# ---- free functions --------------------------------------------------------------------------------------------------
def gen_funcs_unit(g, mod):
    r = g.r
    noun = snake(r.choice(NOUNS[g.kind]))
    fname = unit_file(g, mod, 'funcs_%s' % noun)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), pragma=r.chance(.6))
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    cpp = mfile(g, mod, 'modules/%s/src/%s.cpp' % (mod.name, fname))
    mod.srcs.append(cpp.path)
    cpp.first = hinc
    nso, nsc = g.ns_open(mod)
    hdr.lines += nso
    cpp.lines += nso
    ents = []
    extra = []
    if r.chance(.3):
        pn = r.choice(COMMON_STATIC)
        if pn not in mod.fnames:
            he = Ent(g, 'sfunc', pn, mod, '', ret='int', params=['int'], hdr=None, defsrc=cpp.path)
            extra.append(he)
            cpp.lines.append('static int %s(int p0) { return p0 + %d; }' % (g.tok(he, 'def'), r.rng(1, 9)))
            cpp.lines.append('')
    for _ in range(r.rng(2, 5)):
        nm = g.func_name(mod, avoid=mod.fnames)
        mod.fnames.add(nm)
        variants = []
        seen = set()
        for _v in range(r.wchoice([(1, 6), (2, 3), (3, 1)])):
            ret, ps = rand_sig(g, mod)
            if tuple(ps) in seen:
                continue
            seen.add(tuple(ps))
            variants.append((ret, ps))
        for ret, ps in variants:
            e = Ent(g, 'func', nm, mod, mod.ns, ret=ret, params=ps, hdr=hinc, inline=False)
            e.d = g.doc('result', e)
            if e.d:
                hdr.lines.append(e.d)
            if r.chance(.2):
                e.inline = True
                hdr.lines.append('inline %s %s(%s) { return (%s)(%s); }' % (
                    ret, g.tok(e, 'def'), params_text(ps), ret, ' + '.join(['p%d' % i for i in range(len(ps))] + [str(r.rng(1, 9))])))
            else:
                hdr.lines.append('%s %s(%s);' % (ret, g.tok(e, 'decl'), params_text(ps)))
                e.defsrc = cpp.path
                b = Body(g, cpp, mod, free_ns=mod.ns, extra=extra)
                b.stmts(r.rng(2, 6))
                cpp.lines.append('%s %s(%s) {' % (ret, g.tok(e, 'def'), params_text(ps)))
                for ln in body_lines(g, b, ret, ps):
                    cpp.lines.append(INDENT + ln)
                cpp.lines += ['}', '']
            ents.append(e)
    hdr.lines += nsc
    cpp.lines += nsc
    for e in ents:
        mod.api['funcs'].append(e)


# ---- templates -------------------------------------------------------------------------------------------------------
def gen_tmpl_unit(g, mod):
    r = g.r
    name = g.class_name(mod)
    fname = unit_file(g, mod, snake(name))
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), pragma=r.chance(.6))
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    tc = Ent(g, 'tmplclass', name, mod, mod.ns, hdr=hinc, methods=[], spec=None)
    fld = Ent(g, 'field', 'v_' + str(g.fresh()), mod, '', owner=tc, ftype='T')
    for mn, ps, ret in (('get', [], 'T'), ('set', ['T'], 'void')):
        m = Ent(g, 'method', mn, mod, '', owner=tc, ret=ret, params=ps, const=(mn == 'get'), static=False, virtual=False,
                pure=False, override_of=None, overriders=[], inline=True, hdr=hinc)
        tc.methods.append(m)
    nso, nsc = g.ns_open(mod)
    L = hdr.lines
    L += nso
    d = g.doc('boxed value', tc)
    if d:
        L.append(d)
    L += ['template <typename T>', 'class %s {' % g.tok(tc, 'def'), 'public:']
    L.append(INDENT + 'T %s() const { return %s; }' % (g.tok(tc.methods[0], 'def'), g.tok(fld, 'use')))
    L.append(INDENT + 'void %s(const T& v) { %s = v; }' % (g.tok(tc.methods[1], 'def'), g.tok(fld, 'use')))
    L.append(INDENT + 'T %s{};' % g.tok(fld, 'def'))
    L += ['};', '']
    if r.chance(.55):
        sp = Ent(g, 'tspec', name, mod, mod.ns, hdr=hinc, methods=[], tmpl=tc)
        sf = Ent(g, 'field', 'f_' + str(g.fresh()), mod, '', owner=sp, ftype='bool')
        for mn, ps, ret in (('get', [], 'bool'), ('set', ['bool'], 'void')):
            m = Ent(g, 'method', mn, mod, '', owner=sp, ret=ret, params=ps, const=(mn == 'get'), static=False,
                    virtual=False, pure=False, override_of=None, overriders=[], inline=True, hdr=hinc)
            sp.methods.append(m)
        L += ['template <>', 'class %s<bool> {' % g.tok(sp, 'def'), 'public:',
              INDENT + 'bool %s() const { return %s; }' % (g.tok(sp.methods[0], 'def'), g.tok(sf, 'use')),
              INDENT + 'void %s(bool v) { %s = v; }' % (g.tok(sp.methods[1], 'def'), g.tok(sf, 'use')),
              INDENT + 'bool %s = false;' % g.tok(sf, 'def'), '};', '']
        tc.spec = sp
    for _ in range(r.rng(1, 2)):
        nm = g.func_name(mod, avoid=mod.fnames, common=False)
        mod.fnames.add(nm)
        tf = Ent(g, 'tmplfunc', nm, mod, mod.ns, hdr=hinc, ret='T', params=['T'])
        tf.d = g.doc('twice the argument', tf)
        if tf.d:
            L.append(tf.d)
        L += ['template <typename T>', 'T %s(T x) { return x + x; }' % g.tok(tf, 'def'), '']
        mod.api['tmplfuncs'].append(tf)
    L += nsc
    mod.api['tmplclasses'].append(tc)


# ---- enums, constants, aliases ----------------------------------------------------------------------------------------
def gen_enum_unit(g, mod, c=False):
    r = g.r
    noun = snake(r.choice(NOUNS[g.kind]))
    fname = unit_file(g, mod, 'types_%s' % noun)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), pragma=r.chance(.6), cguard=c)
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    nso, nsc = g.ns_open(mod)
    L = hdr.lines
    L += nso
    for _ in range(r.rng(1, 2)):
        en_name = g.class_name(mod) if not c else g.unique_name('%s%s_%s' % (mod.prefix, noun, r.choice(['kind', 'mode', 'state', 'flags'])), 'cnames')
        scoped = (not c) and r.chance(.6)
        en = Ent(g, 'enum', en_name, mod, mod.ns, scoped=scoped, enumerators=[], hdr=hinc)
        words = r.sample(COMMON_ENUMERATORS, r.rng(3, 5))
        items = []
        for w in words:
            if c:
                nm = (mod.prefix + en_name + '_' + w).upper() if False else (mod.prefix.upper() + w.upper() + '_' + str(g.fresh()))
            elif scoped:
                nm = w
            else:
                nm = '%s_%s' % (en_name, w)
            e = Ent(g, 'enumerator', nm, mod, mod.ns if not scoped else '', owner=en if scoped else None, enum=en)
            en.enumerators.append(e)
            items.append(INDENT + g.tok(e, 'def') + ',')
        d = g.doc('enumeration', en)
        if d:
            L.append(d)
        L.append(('enum class %s {' if scoped else 'enum %s {') % g.tok(en, 'def'))
        L += items
        L += ['};', '']
        mod.api['enums'].append(en)
    for _ in range(r.rng(1, 3)):
        t = r.choice(TYPES_C if c else [x for x in TYPES_CXX if x[0] != 'bool'])
        nm = ('%s%s_%d' % (mod.prefix, r.choice(['limit', 'step', 'cap', 'base', 'scale']), g.fresh())) if c else \
            'k%s%d' % (r.choice(['Limit', 'Step', 'Cap', 'Base', 'Scale', 'Margin']), g.fresh())
        ce = Ent(g, 'const', nm, mod, mod.ns, ctype=t[0], hdr=hinc)
        d = g.doc('constant', ce)
        if d:
            L.append(d)
        L.append(('static const %s %s = %s;' if c else 'constexpr %s %s = %s;') % (t[0], g.tok(ce, 'def'), t[1]))
        mod.api['consts'].append(ce)
    L.append('')
    for _ in range(r.rng(1, 2)):
        t = r.choice(['unsigned long', 'double', 'long', 'unsigned'])
        nm = ('%s%s_%d_t' % (mod.prefix, r.choice(['id', 'real', 'handle', 'size']), g.fresh())) if c else \
            r.choice(['Id', 'Real', 'Handle', 'Size', 'Count']) + str(g.fresh())
        ae = Ent(g, 'alias', nm, mod, mod.ns, target=t, hdr=hinc)
        if c or r.chance(.4):
            L.append('typedef %s %s;' % (t, g.tok(ae, 'def')))
        else:
            L.append('using %s = %s;' % (g.tok(ae, 'def'), t))
        mod.api['aliases'].append(ae)
    L += nsc


# ---- macros ----------------------------------------------------------------------------------------------------------
def gen_macro_unit(g, mod, c=False):
    r = g.r
    noun = snake(r.choice(NOUNS[g.kind]))
    fname = unit_file(g, mod, 'macros_%s' % noun)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), pragma=r.chance(.5), cguard=False)
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    pre = (mod.prefix if c else mod.name + '_').upper()
    L = hdr.lines
    for _ in range(r.rng(1, 3)):
        e = Ent(g, 'macro', '%s%s_%d' % (pre, r.choice(NOUNS[g.kind]).upper(), g.fresh()), mod, '', hdr=hinc, fn=False,
                value=str(r.rng(2, 99)))
        d = g.doc('limit', e)
        if d:
            L.append(d)
        L.append('#define %s %s' % (g.tok(e, 'def'), e.value))
        mod.api['macros'].append(e)
    for _ in range(r.rng(1, 2)):
        e = Ent(g, 'macro', '%sSCALE_%d' % (pre, g.fresh()), mod, '', hdr=hinc, fn=True)
        L.append('#define %s(x) ((x) * %d)' % (g.tok(e, 'def'), r.rng(2, 9)))
        mod.api['fmacros'].append(e)
    # a macro that declares functions
    dm = Ent(g, 'macro', '%sDECLARE_FN_%d' % (pre, g.fresh()), mod, '', hdr=hinc, fn=True)
    L.append('#define %s(fname, mul) %sint fname(int v) { return v * (mul); }' % (g.tok(dm, 'def'), 'static inline ' if c else 'inline '))
    nso, nsc = g.ns_open(mod)
    L += [''] + nso
    for _ in range(r.rng(1, 3)):
        nm = ('%s%s_%d' % (mod.prefix, r.choice(VERBS), g.fresh())) if c else r.choice(VERBS) + r.choice(NOUNS[g.kind]) + str(g.fresh())
        fe = Ent(g, 'func', nm, mod, mod.ns, ret='int', params=['int'], hdr=hinc, mfunc=True, inline=True, cstatic=c, pname='v')
        L.append('%s(%s, %d)' % (g.tok(dm, 'use'), g.tok(fe, 'def'), r.rng(2, 9)))
        mod.api['funcs'].append(fe)
    L += nsc


# ---- tables (long files) ---------------------------------------------------------------------------------------------
def gen_table_unit(g, mod, c=False):
    r = g.r
    n = g.fresh()
    base = 'table%d' % n
    fname = unit_file(g, mod, base)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), cguard=c)
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    ext = '.c' if c else '.cpp'
    cpp = mfile(g, mod, 'modules/%s/src/%s%s' % (mod.name, fname, ext))
    mod.srcs.append(cpp.path)
    cpp.first = hinc
    K = g.table_fns
    an = ('%stable%d_data' % (mod.prefix, n)) if c else 'kTable%d' % n
    arr = Ent(g, 'var', an, mod, mod.ns, vtype='const int', hdr=hinc)
    nso, nsc = g.ns_open(mod)
    hdr.lines += nso + ['extern const int %s[];' % g.tok(arr, 'decl'), '']
    cpp.lines += nso
    fns = []
    for i in range(K):
        nm = ('%stable%d_fn_%d' % (mod.prefix, n, i)) if c else 'table%d_fn_%d' % (n, i)
        e = Ent(g, 'func', nm, mod, mod.ns, ret='int', params=['int'], hdr=hinc, tablefn=True, pname='v')
        e.tidx = 0 if i == 0 else (1 if i == K // 2 else (2 if i == K - 1 else 9))
        e.defsrc = cpp.path
        fns.append(e)
        hdr.lines.append('int %s(int v);' % g.tok(e, 'decl'))
    # data
    cpp.lines.append('/* Generated data table %d. */' % n)
    cpp.lines.append('const int %s[] = {' % g.tok(arr, 'def'))
    L = max(8, K * 2)
    for i in range(L):
        cpp.lines.append(INDENT + ', '.join(str(r.rng(0, 9999)) for _ in range(8)) + ',')
    cpp.lines += ['};', '']
    for i, e in enumerate(fns):
        cpp.lines.append('int %s(int v) { return v * %d + %s[%d]; }' % (g.tok(e, 'def'), i % 7 + 1, g.tok(arr, 'use'), i % 8))
    cpp.lines.append('')
    tn = 'table%d_fn_t' % n
    cpp.lines.append('typedef int (*%s)(int);' % tn)
    cpp.lines.append('static const %s table%d_fns[] = {' % (tn, n))
    for e in fns:
        cpp.lines.append(INDENT + '&%s,' % g.tok(e, 'use'))
    cpp.lines += ['};', '']
    hdr.lines += nsc
    cpp.lines += nsc
    for e in r.sample(fns, 3):
        mod.api['funcs'].append(e)


# ---- deep include chains ---------------------------------------------------------------------------------------------
def gen_chain_unit(g, mod, c=False):
    r = g.r
    n = g.fresh()
    depth = g.chain_depth
    key = '%schain%d' % (mod.prefix if c else '', n)
    fns = []
    nso, nsc = g.ns_open(mod)
    files = []
    for i in range(depth + 1):
        f = mfile(g, mod, 'modules/%s/include/%s/chain%d/c%d.h' % (mod.name, mod.name, n, i), cguard=False)
        files.append(f)
    top_inc = '%s/chain%d/c0.h' % (mod.name, n)
    mod.hdrs.append(top_inc)
    for i in range(depth + 1):
        e = Ent(g, 'func', '%s_l%d' % (key, i), mod, mod.ns, ret='int', params=['int'],
                hdr='%s/chain%d/c%d.h' % (mod.name, n, i), inline=True, chain=True, cstatic=c, pname='v')
        fns.append(e)
    entry = Ent(g, 'func', '%s_entry' % key, mod, mod.ns, ret='int', params=['int'], hdr=top_inc, pname='v')
    inl = 'static inline' if c else 'inline'
    for i, f in enumerate(files):
        if i + 1 <= depth:
            f.inc('%s/chain%d/c%d.h' % (mod.name, n, i + 1))
        f.lines += nso
        if i == 0:
            f.lines.append('/** Entry point of the include chain %d (%d headers deep). */' % (n, depth + 1))
            f.lines.append('int %s(int v);' % g.tok(entry, 'decl'))
        if i == depth:
            f.lines.append('%s int %s(int v) { return v + 1; }' % (inl, g.tok(fns[i], 'def')))
        else:
            f.lines.append('%s int %s(int v) { return %s(v) + %d; }' % (inl, g.tok(fns[i], 'def'), g.tok(fns[i + 1], 'use'), i))
        f.lines += nsc
    cpp = mfile(g, mod, 'modules/%s/src/chain%d.%s' % (mod.name, n, 'c' if c else 'cpp'))
    mod.srcs.append(cpp.path)
    cpp.first = top_inc
    cpp.lines += nso
    cpp.lines += ['int %s(int v) {' % g.tok(entry, 'def'), INDENT + 'return %s(v) * 2;' % g.tok(fns[0], 'use'), '}'] + nsc
    mod.api['funcs'].append(entry)
    mod.api['funcs'].append(fns[0])
    mod.api['chains'].append(fns[-1])
    # keep the deepest function reachable for a direct call too
    mod.api['funcs'].append(fns[depth // 2])


# ---- C -----------------------------------------------------------------------------------------------------------------
def gen_cstruct_unit(g, mod):
    r = g.r
    noun = r.choice(NOUNS['c']).lower()
    tag = g.unique_name('%s%s' % (mod.prefix, noun), 'ctag')
    fname = unit_file(g, mod, tag)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), cguard=True, pragma=r.chance(.5))
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    cpp = mfile(g, mod, 'modules/%s/src/%s.c' % (mod.name, fname))
    mod.srcs.append(cpp.path)
    cpp.first = hinc
    tagent = Ent(g, 'cstruct', tag, mod, '', hdr=hinc)
    st = Ent(g, 'alias', tag + '_t', mod, '', hdr=hinc, target='struct ' + tag, methods=[], fields=[], tagent=tagent)
    L = hdr.lines
    d = g.doc(noun, tagent)
    if d:
        L.append(d)
    L.append('struct %s {' % g.tok(tagent, 'def'))
    for _ in range(r.rng(2, 4)):
        w = r.choice(FIELD_WORDS)
        if any(f.name == w for f in st.fields):
            continue
        t = r.choice(TYPES_C)[0]
        f_ = Ent(g, 'field', w, mod, '', owner=st, ftype=t)
        st.fields.append(f_)
        L.append(INDENT + '%s %s;' % (t, g.tok(f_, 'def')))
    L += ['};', 'typedef struct %s %s;' % (g.tok(tagent, 'use'), g.tok(st, 'def')), '']
    ms = []
    verbs = r.sample(sorted(set(COMMON_METHODS + VERBS)), r.rng(3, 5))
    for v in verbs:
        ret, ps = rand_sig(g, mod, c=True)
        m = Ent(g, 'func', '%s_%s' % (tag, v), mod, '', ret=ret, params=['self'] + ps, hdr=hinc, cstructm=st)
        ms.append(m)
        st.methods.append(m)
        dd = g.doc('value', m)
        if dd:
            L.append(dd)
        L.append('%s %s(%s* self%s);' % (ret, g.tok(m, 'decl'), g.tok(st, 'use'), ''.join(', %s p%d' % (t, i + 1) for i, t in enumerate(ps))))
    cpp.lines.append('')
    extra = []
    if r.chance(.4):
        pn = r.choice(COMMON_STATIC)
        he = Ent(g, 'sfunc', pn, mod, '', ret='int', params=['int'], hdr=None, defsrc=cpp.path)
        extra.append(he)
        cpp.lines.append('static int %s(int p0) { return p0 + %d; }' % (g.tok(he, 'def'), r.rng(1, 9)))
        cpp.lines.append('')
    for m in ms:
        b = Body(g, cpp, mod, cmode=True, extra=extra)
        b.stmts(r.rng(1, 4))
        cpp.lines.append('%s %s(%s* self%s) {' % (m.ret, g.tok(m, 'def'), g.tok(st, 'use'), ''.join(', %s p%d' % (t, i + 1) for i, t in enumerate(m.params[1:]))))
        pre = ['double r = 0;']
        for f_ in st.fields:
            if r.chance(.6):
                pre.append('r += self->%s;' % g.tok(f_, 'use'))
        for i in range(len(m.params) - 1):
            pre.append('r += p%d;' % (i + 1))
        for ln in pre + b.lines + ['return (%s)(r);' % m.ret]:
            cpp.lines.append(INDENT + ln)
        cpp.lines += ['}', '']
    mod.api['cstructs'].append(st)


def gen_cfuncs_unit(g, mod):
    r = g.r
    noun = r.choice(NOUNS['c']).lower()
    fname = unit_file(g, mod, 'funcs_%s' % noun)
    hdr = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, fname), cguard=True, pragma=r.chance(.5))
    hinc = '%s/%s.h' % (mod.name, fname)
    mod.hdrs.append(hinc)
    cpp = mfile(g, mod, 'modules/%s/src/%s.c' % (mod.name, fname))
    mod.srcs.append(cpp.path)
    cpp.first = hinc
    extra = []
    if r.chance(.4):
        pn = r.choice(COMMON_STATIC)
        he = Ent(g, 'sfunc', pn, mod, '', ret='int', params=['int'], hdr=None, defsrc=cpp.path)
        extra.append(he)
        cpp.lines.append('static int %s(int p0) { return p0 + %d; }' % (g.tok(he, 'def'), r.rng(1, 9)))
        cpp.lines.append('')
    for _ in range(r.rng(2, 5)):
        nm = g.unique_name('%s%s_%s' % (mod.prefix, r.choice(VERBS), noun), 'cfn')
        ret, ps = rand_sig(g, mod, c=True)
        e = Ent(g, 'func', nm, mod, '', ret=ret, params=ps, hdr=hinc, inline=False)
        e.d = g.doc('result', e)
        if e.d:
            hdr.lines.append(e.d)
        ptxt = params_text(ps) or 'void'
        if r.chance(.15):
            e.inline = True
            e.cstatic = True
            hdr.lines.append('static inline %s %s(%s) { return (%s)(%s); }' % (
                ret, g.tok(e, 'def'), ptxt, ret, ' + '.join(['p%d' % i for i in range(len(ps))] + [str(r.rng(1, 9))])))
        else:
            hdr.lines.append('%s %s(%s);' % (ret, g.tok(e, 'decl'), ptxt))
            e.defsrc = cpp.path
            b = Body(g, cpp, mod, cmode=True, extra=extra)
            b.stmts(r.rng(2, 6))
            cpp.lines.append('%s %s(%s) {' % (ret, g.tok(e, 'def'), ptxt))
            for ln in body_lines(g, b, ret, ps, c=True):
                cpp.lines.append(INDENT + ln)
            cpp.lines += ['}', '']
        mod.api['funcs'].append(e)
# ---- modules and application layer --------------------------------------------------------------------------------------
def setup_modules(g):
    r = g.r
    total = g.size
    app_files = max(3, total // 40)
    avg = int(30 + (total ** 0.5) / 2)
    nmods = max(4, min(400, int(round((total - app_files) / float(avg)))))
    top = {'game': 'gx', 'service': 'svc', 'lib': 'lx', 'c': '', 'mixed': 'mx'}[g.profile]
    words_cxx = MOD_WORDS['service' if g.profile == 'mixed' else g.kind]
    words_c = MOD_WORDS['c']
    nc = nx = 0
    prefixes = set()
    for i in range(nmods):
        lang = 'c' if (g.profile == 'c' or (g.profile == 'mixed' and i % 3 == 0)) else 'cxx'
        if lang == 'c':
            words, k = words_c, nc
            nc += 1
        else:
            words, k = words_cxx, nx
            nx += 1
        nm = words[k % len(words)]
        cyc = k // len(words)
        if cyc:
            nm += '_' + MOD_SUFFIX[(cyc - 1) % len(MOD_SUFFIX)] + ('' if cyc <= len(MOD_SUFFIX) else str(cyc))
        mod = Mod(i, nm, lang)
        g.reserved.add(nm)
        mod.snake = (g.kind != 'game') and r.chance(.5)
        mod.style = r.below(2)
        if lang == 'cxx':
            mod.ns = '%s::%s' % (top, nm)
        else:
            p = nm[:3] + '_'
            k2 = 3
            while p in prefixes:
                k2 += 1
                p = nm[:k2] + '_' if k2 <= len(nm) else nm + str(g.fresh()) + '_'
            prefixes.add(p)
            mod.prefix = p
        pool = [m for m in g.mods if (lang == 'cxx' or m.lang == 'c')]
        if pool:
            nd = r.rng(1, min(4, len(pool)))
            for _ in range(nd):
                x = r.rand()
                d = pool[int(len(pool) * (1 - x * x)) if r.chance(.5) else int(len(pool) * x * x)]
                d = pool[min(len(pool) - 1, int(len(pool) * x ** 1.5))] if r.chance(.4) else d
                if d not in mod.deps:
                    mod.deps.append(d)
        g.mods.append(mod)
    rest = total - app_files
    ws = [r.rng(60, 140) for _ in g.mods]
    sw = float(sum(ws))
    for m, w in zip(g.mods, ws):
        m.budget = max(6, int(rest * w / sw))
    for m in g.mods:
        seen = set()
        todo = list(m.deps)
        while todo:
            d = todo.pop()
            if d.idx in seen:
                continue
            seen.add(d.idx)
            todo += d.deps
        m.closure = [x for x in g.mods if x.idx in seen]
    g.app_files = app_files


def gen_module(g, mod):
    r = g.r
    c = mod.lang == 'c'
    wl = W_C if c else W_CXX[g.kind]
    items = [(k, w) for k, w in wl.items() if w > 0]
    first = True
    while mod.nfiles < mod.budget:
        if first:
            k = 'cenum' if c else 'enum'
            first = False
        else:
            k = r.wchoice(items)
        if k == 'cls':
            gen_class_unit(g, mod, iface=r.chance(.22) and HIER[g.profile] > 0)
        elif k == 'funcs':
            gen_funcs_unit(g, mod)
        elif k == 'tmpl':
            gen_tmpl_unit(g, mod)
        elif k == 'enum':
            gen_enum_unit(g, mod)
        elif k == 'macro':
            gen_macro_unit(g, mod)
        elif k == 'table':
            gen_table_unit(g, mod)
        elif k == 'chain':
            gen_chain_unit(g, mod)
        elif k == 'cstruct':
            gen_cstruct_unit(g, mod)
        elif k == 'cfuncs':
            gen_cfuncs_unit(g, mod)
        elif k == 'cenum':
            gen_enum_unit(g, mod, c=True)
        elif k == 'cmacro':
            gen_macro_unit(g, mod, c=True)
        elif k == 'ctable':
            gen_table_unit(g, mod, c=True)
        elif k == 'cchain':
            gen_chain_unit(g, mod, c=True)
        mod.units += 1
    if mod.hdrs:
        um = mfile(g, mod, 'modules/%s/include/%s/%s.h' % (mod.name, mod.name, mod.name), pragma=True)
        um.lines.append('// Umbrella header of module %s.' % mod.name)
        for h in mod.hdrs[:10]:
            um.inc(h)
        mod.umbrella = '%s/%s.h' % (mod.name, mod.name)


def gen_app(g):
    r = g.r
    c = g.profile == 'c'
    app = Mod(10 ** 6, 'app', 'c' if c else 'cxx')
    cands = [m for m in g.mods if m.hdrs]
    app.deps = r.sample(cands, min(len(cands), 6)) if cands else []
    seen = set()
    todo = list(app.deps)
    while todo:
        d = todo.pop()
        if d.idx in seen:
            continue
        seen.add(d.idx)
        todo += d.deps
    app.closure = [x for x in g.mods if x.idx in seen]
    ext = '.c' if c else '.cpp'
    nfiles = max(2, g.app_files - 1)
    app_h = g.file('app/app.h', header=True, cguard=c)
    app_h.lines.append('// Application layer: entry points calling into several libraries.')
    main = g.file('app/main' + ext, header=False)
    main.first = 'app.h'
    fns = []
    for i in range(nfiles):
        f = g.file('app/app_%d%s' % (i, ext), header=False)
        f.first = 'app.h'
        app.srcs.append(f.path)
        for d in r.sample(app.deps, min(2, len(app.deps))):
            if getattr(d, 'umbrella', None):
                f.inc(d.umbrella)
        for _ in range(r.rng(1, 3)):
            e = Ent(g, 'func', 'appRun%d' % g.fresh(), app, '', ret='double', params=[], hdr='app.h')
            fns.append(e)
            b = Body(g, f, app, cmode=c)
            b.stmts(r.rng(10, 24))
            app_h.lines.append('double %s(%s);' % (g.tok(e, 'decl'), 'void' if c else ''))
            f.lines.append('double %s(%s) {' % (g.tok(e, 'def'), 'void' if c else ''))
            for ln in ['double r = 0;'] + b.lines + ['return r;']:
                f.lines.append(INDENT + ln)
            f.lines += ['}', '']
    app.srcs.append(main.path)
    main.lines.append('int main(%s) {' % ('void' if c else ''))
    main.lines.append(INDENT + 'double r = 0;')
    for e in fns:
        main.lines.append(INDENT + 'r += %s();' % g.tok(e, 'use'))
    main.lines += [INDENT + 'return (int)r;', '}']
    g.app = app


# ---- finalize: counts, tracking, anchors --------------------------------------------------------------------------------
def category(e):
    k = e.kind
    if k == 'func':
        if getattr(e, 'mfunc', False):
            return 'mfunc'
        if getattr(e, 'tablefn', False):
            return 'tablefn'
        if getattr(e, 'chain', False):
            return 'chainfn'
        return 'func'
    if k == 'method':
        if e.owner.kind != 'class':
            return None
        if e.virtual:
            return 'vmethod'
        return 'smethod' if e.static else 'method'
    if k == 'class':
        return None if e.explicit_special else 'class'
    if k in ('const', 'enum', 'enumerator', 'alias', 'static', 'tmplclass', 'tmplfunc', 'tspec', 'nested', 'sfunc',
             'var', 'cstruct'):
        if k == 'alias' and hasattr(e, 'tagent'):
            return None
        return k
    if k == 'macro':
        return 'fmacro' if e.fn else 'macro'
    if k == 'field':
        if e.owner is not None and e.owner.kind in ('class', 'alias') and not getattr(e, 'link', None) \
                and e.ftype is not None:
            return 'field'
        return None
    return None


def count_markers(g):
    modlang = dict((m.name, m.lang) for m in g.mods)
    for path in sorted(g.files):
        if path.endswith('.cpp'):
            lang = 'cxx'
        elif path.endswith('.c'):
            lang = 'c'
        else:
            parts = path.split('/')
            lang = modlang.get(parts[1], 'cxx') if parts[0] == 'modules' else 'cxx'
        for ln in g.files[path].lines:
            if '\x01' in ln:
                for m in MARK.finditer(ln):
                    e = g.ents[int(m.group(1))]
                    e.counts[m.group(2)] = e.counts.get(m.group(2), 0) + 1
                    if m.group(2) == 'use':
                        e.uselangs.add(lang)


def eligible(e, cat):
    nu = e.counts.get('use', 0)
    if cat == 'vmethod':
        return bool(e.overriders) or e.pure
    if cat in ('class',):
        return nu >= 1 and e.counts.get('def', 0) == 1
    if cat == 'tablefn':
        return e.tidx < 9
    if cat == 'privfn':
        return nu >= 1
    return nu >= 1


def transitive(e, attr):
    out = []
    seen = set()
    todo = list(getattr(e, attr))
    while todo:
        x = todo.pop(0)
        if x.id in seen:
            continue
        seen.add(x.id)
        out.append(x)
        todo += getattr(x, attr)
    return sorted(out, key=lambda z: z.id)


def choose_tracking(g):
    r = g.r
    cats = {}
    for e in g.ents:
        c = category(e)
        if c is None:
            continue
        e.cat = c
        cats.setdefault(c, []).append(e)
    S = g.samples
    for c in sorted(cats):
        cand = [e for e in cats[c] if eligible(e, c)]
        r.shuffle(cand)
        n = S if c not in ('smethod', 'sfunc', 'tspec', 'nested', 'var', 'cstruct', 'tmplfunc', 'tmplclass', 'static',
                           'mfunc', 'tablefn', 'chainfn', 'fmacro') else max(2, S // 2)
        for e in cand[:n]:
            e.tracked = True
            e.primary = True
    # impl targets: overriders and derived classes must carry anchors as well
    for e in g.ents:
        if e.tracked and e.cat == 'vmethod':
            for o in transitive(e, 'overriders'):
                if not o.tracked:
                    o.tracked = True
                    o.cat = o.cat or 'vmethod'
    # base classes with derived classes
    bases = [e for e in g.ents if e.kind == 'class' and e.derived and not e.explicit_special and e.counts.get('def', 0) == 1]
    r.shuffle(bases)
    g.impl_bases = bases[:max(2, S // 2)]
    for b in g.impl_bases:
        b.tracked = True
        b.primary = True
        b.cat = b.cat or 'class'
        for d in transitive(b, 'derived'):
            d.tracked = True
            d.cat = d.cat or 'class'
    # helper entities for rename / typedef come from the same sets
    for e in g.ents:
        if e.tracked:
            e.aid = re.sub('[^A-Za-z0-9_]', '_', e.name) + '_' + str(e.id)


def substitute(g):
    ents = g.ents

    def sub(m):
        e = ents[int(m.group(1))]
        if not e.tracked:
            return ''
        role = m.group(2)
        n = len(e.anchors.get(role, []))
        cap = USE_CAP if role == 'use' else (6 if role == 'var' else 400)
        if n >= cap:
            e.capped = True
            return ''
        nm = '%s_%s%d' % (e.aid, role, n)
        e.anchors.setdefault(role, []).append(nm)
        return '/*@%s*/' % nm

    out = {}
    for path in sorted(g.files):
        f = g.files[path]
        f.lines = [MARK.sub(sub, ln) if '\x01' in ln else ln for ln in f.lines]


# ---- expectations --------------------------------------------------------------------------------------------------------
def A(e, role, k=0):
    a = e.anchors.get(role, [])
    return a[k] if len(a) > k else None


def Dn(e):
    return A(e, 'def') or A(e, 'decl')


def Dc(e):
    return A(e, 'decl') or A(e, 'def')


def alla(e, roles=('decl', 'def', 'fwd', 'use')):
    return [a for ro in roles for a in e.anchors.get(ro, [])]


MACRO_REASON = 'fast tier does not index macros (W9c P-1): no answer for a #define'
MFUNC_REASON = 'fast tier: names declared through macro arguments are not found (W9c P-2)'


HTYPE = {'int': 'int', 'long': 'long', 'double': 'double', 'bool': 'bool', 'unsigned': 'unsigned int'}


def hover_strs(g, e):
    """Substrings the hover text (clangd's markdown) must contain; the formats were taken from clangd 22 output."""
    k = e.kind
    out = [e.name]
    if k in ('func', 'method'):
        if e.ret in HTYPE:
            out.append('\u2192 ' + HTYPE[e.ret])
        ps = [p for p in e.params if p in HTYPE]
        if ps and e.params[0] != 'self':
            out.append('- %s %s' % (HTYPE[ps[0]], getattr(e, 'pname', 'p0')))
    elif k == 'const':
        out.append('Type: const ' + HTYPE.get(e.ctype, e.ctype))
    elif k == 'field':
        out.append('Type: ' + HTYPE.get(e.ftype, e.ftype))
    elif k == 'static':
        out.append('Type: int')
    elif k == 'alias':
        out.append('Type: ' + e.target)
    elif k == 'class':
        out = ['class ' + e.name]
    elif k == 'enum':
        out = ['enum ' + e.name]
    elif k == 'enumerator':
        out = ['enumerator ' + e.name]
    elif k == 'macro':
        out = ['#define ' + e.name + ('(x)' if e.fn else ' ' + e.value)]
    # (not asserted: doc comments - the anchor comment between the doc comment and the name keeps clangd from attaching
    #  them - and the "provided by" header - clangd omits it when the use is in the declaring header's own source)
    return out


def build_expect(g):
    r = g.r
    S = g.samples
    # clang's USR of a `static` function contains only the file's base name: equal names in equally named files of
    # different directories are one symbol for clangd (STRESS_RESULTS G-4)
    sf = {}
    for e in g.ents:
        if e.kind == 'sfunc':
            sf.setdefault((e.name, os.path.basename(e.defsrc)), set()).add(e.defsrc)
    for e in g.ents:
        if e.kind == 'sfunc':
            # (C only: in C++ the USR also holds the namespace, which differs per module)
            e.usr_clash = e.defsrc.endswith('.c') and len(sf[(e.name, os.path.basename(e.defsrc))]) > 1
    fastl = []
    mainl = []
    renames = []
    ntyped = 0
    tracked = [e for e in g.ents if e.tracked and e.cat]
    kinds_primary = set()

    def pick_use(e):
        us = e.anchors.get('use', [])
        return us[r.below(len(us))] if us else None

    def fast_def(e, u):
        tg = [a for a in (Dc(e), Dn(e)) if a]
        tg = sorted(set(tg), key=tg.index)
        if e.cat in ('macro', 'fmacro'):
            fastl.append('xfail fast def %s -> %s  # %s' % (u, Dn(e), MACRO_REASON))
        elif e.cat == 'mfunc':
            fastl.append('xfail fast def %s -> %s  # %s' % (u, Dn(e), MFUNC_REASON))
        elif g.census.get(e.name, 0) == 1:
            fastl.append('fast def %s -> %s' % (u, ' | '.join(tg)))
        else:
            # the word has several definitions: the scope-aware fast tier (FastScope) is expected to pick this one;
            # whatever it still gets wrong becomes an `xfail` in a committed corpus (xfail_from_report.py)
            fastl.append('fast def %s -> %s' % (u, Dn(e)))

    for e in tracked:
        cat = e.cat
        if not e.primary:
            continue
        u = pick_use(e)
        if cat not in ('macro', 'fmacro') and g.census.get(e.name, 0) == 1 and g.census_ci.get(e.name.lower(), 0) == 1 \
                and e.kind not in ('enumerator',) and r.chance(.7) \
                and cat not in ('mfunc',):
            tg = sorted(set(a for a in (Dc(e), Dn(e)) if a))
            if e.kind != 'field' or True:
                fastl.append('fast sym "%s" -> %s' % (e.name, ' | '.join(tg)))
        if u:
            fast_def(e, u)
            if g.census.get(e.name, 0) == 1 and cat not in ('macro', 'fmacro', 'mfunc') and r.chance(.5):
                fastl.append('fast hover %s ~ "%s"' % (u, e.name))
        # ---- final answers
        if u:
            mainl.append('def %s -> %s' % (u, Dn(e)))
            if cat not in ('macro', 'fmacro', 'class', 'cstruct', 'tmplclass', 'tspec', 'nested') or False:
                if Dc(e) and e.kind not in ('enumerator',):
                    mainl.append('decl %s -> %s' % (u, Dc(e)))
        roles = ('decl', 'def', 'fwd', 'use')
        al = alla(e, roles)
        mixed_static = getattr(e, 'cstatic', False) and g.profile == 'mixed'
        if al:
            op = '>=' if (e.capped or cat in ('vmethod',) or e.kind in ('tmplclass', 'tspec', 'tmplfunc')) else '='
            q = Dc(e) or al[0]
            if getattr(e, 'usr_clash', False):
                mainl.append('xfail refs %s %s %s  # clangd: the USR of a static function holds only the base name of its file, '
                             'same-named static functions in same-named files of other directories are merged' % (q, op, ' '.join(al)))
            elif mixed_static and 'c' in e.uselangs and 'cxx' in e.uselangs and op == '=':
                mainl.append('xfail refs %s = %s  # clangd: a static inline function of a C header used from both C and C++ '
                             'translation units has two USRs, the uses in the other language are not found' % (q, ' '.join(al)))
            elif mixed_static:
                mainl.append('refs %s >= %s' % (q, ' '.join(e.anchors.get('def', [])) or q))
            else:
                mainl.append('refs %s %s %s' % (q, op, ' '.join(al)))
        if u and cat not in ('macro', 'fmacro'):
            hs = hover_strs(g, e)
            mainl.append('hover %s ~ %s' % (u, ' '.join('"%s"' % h for h in hs)))
        if cat not in ('macro', 'fmacro') and g.census.get(e.name, 0) == 1 and g.census_ci.get(e.name.lower(), 0) == 1 \
                and e.kind not in ('enumerator', 'field') and r.chance(.8):
            mainl.append('sym "%s" -> %s' % (e.name, Dn(e)))
        if 'var' in e.anchors:
            mainl.append('typedef %s -> %s' % (e.anchors['var'][0], A(e, 'def')))
        # rename candidates
        if (not e.capped and not mixed_static and not getattr(e, 'usr_clash', False) and 1 <= len(al) <= 8 and e.kind in ('func', 'method', 'const', 'static', 'alias', 'class', 'field',
                                                                'tmplfunc', 'enum')
                and cat not in ('vmethod', 'mfunc', 'tablefn', 'chainfn') and e.cat != 'smethod' or False):
            if not (e.kind == 'method' and (e.virtual or e.override_of)):
                renames.append((e, al))
    # impl: the answer lands on the definition, or on the declaration / forward declaration when the header that holds it
    # is open in the editor (clangd's dynamic index shadows the background index, STRESS_RESULTS G-1).  Whether it is
    # open depends on earlier navigation and on timing, so lines whose targets have such a second spelling are
    # "unstable" and are placed first (before any navigation) or last, see --impl-position.
    impl_stable = []
    impl_unstable = []

    def impl_line(kind, q, op, targets, others, comment=''):
        unstable = any(('decl' in o.anchors or 'fwd' in o.anchors) for o in others)
        (impl_unstable if unstable else impl_stable).append('%s%s %s %s %s%s' % (
            'xfail ' if comment else '', 'impl', q, op, ' '.join(targets), ('  # ' + comment) if comment else ''))

    for e in tracked:
        if e.cat == 'vmethod' and e.overriders:
            direct = [Dn(o) for o in e.overriders]
            allo = [Dn(o) for o in transitive(e, 'overriders')]
            q = Dc(e)
            u = pick_use(e)
            if set(direct) == set(allo):
                impl_line('impl', q, '=', direct, e.overriders)
            else:
                impl_line('impl', q, '>=', direct, e.overriders)
                impl_line('impl', q, '=', allo, transitive(e, 'overriders'),
                          'clangd findImplementations is one level only (W9d finding 10): indirect overriders are missing')
            # (asked at a use, the open .cpp has the overriders' headers in its preamble: only without a second spelling)
            if u and not any(('decl' in o.anchors or 'fwd' in o.anchors) for o in e.overriders):
                impl_line('impl', u, '>=', direct, e.overriders)
    for b in g.impl_bases:
        direct = [Dn(d) for d in b.derived]
        allo = [Dn(d) for d in transitive(b, 'derived')]
        q = Dn(b)
        if set(direct) == set(allo):
            impl_line('impl', q, '=', direct, b.derived)
        else:
            impl_line('impl', q, '>=', direct, b.derived)
            impl_line('impl', q, '=', allo, transitive(b, 'derived'),
                      'clangd findImplementations is one level only (W9d finding 10): indirect subclasses are missing')
    # edits
    edits = []
    edit_ents = []
    cand = [e for e in tracked if e.cat == 'func' and getattr(e, 'defsrc', None) and e.counts.get('use')]
    ne = 0
    used_files = set()
    for e in cand:
        if ne >= max(2, S // 2):
            break
        if e.defsrc in used_files or e.defsrc not in g.final_text:
            continue
        used_files.add(e.defsrc)
        txt = g.final_text[e.defsrc]
        nl = txt.count('\n') + 1
        ne += 1
        ea = 'ed%d_use' % ne
        args = ', '.join(lit(t) for t in e.params) if e.params else ''
        qual = (e.ns + '::') if e.ns else ''
        body = 'int edit_fn_%d() { return (int)%s/*@%s*/%s(%s); }\\n' % (ne, qual, ea, e.name, args)
        edits.append('open %s' % e.defsrc)
        edits.append('edit %s:%d:1 "%s"' % (e.defsrc, nl, body))
        edits.append('def %s -> %s' % (ea, Dn(e)))
        edits.append('decl %s -> %s' % (ea, Dc(e)))
        edits.append('refs %s >= %s %s' % (Dc(e), ' '.join(alla(e)), ea))
        g.edit_anchors.append(ea)
        edit_ents.append(e)
    # renames (a few), last: in the medium / large runs a series of renames can leave the session unanswering
    # (STRESS_RESULTS G-2), and the entities used by the edits above must not be renamed (the edit anchors would change)
    renl = []
    edited = set(id(e) for e in edit_ents)
    for e, al in renames:
        if id(e) in edited:
            continue
        if len(renl) >= 2 * max(2, S):
            break
        nn = 'rn%d_x' % e.id
        renl.append('rename %s %s' % (Dc(e) or al[0], nn))
        renl.append('  ' + ' '.join(al))
    hdr = ['# Generated by tools/stress/gen_codebase.py: profile=%s size=%d seed=%d variant=%s samples=%d' % (
        g.profile, g.size, g.seed, g.variant, S),
        '# The answers come from the generator\'s own symbol table (every definition and use it wrote), not from any tool.',
        '# `xfail fast` marks names the fast (lexer) tier cannot know: macros and names declared through macro arguments.',
        '']
    first_blk = []
    last_blk = []
    if impl_unstable:
        note = ['# ---- implementations whose answer depends on which headers are open (STRESS_RESULTS G-1)']
        if g.impl_position == 'first':
            first_blk = note + impl_unstable + ['']
        else:
            last_blk = [''] + note + impl_unstable
    return (hdr + first_blk + ['# ---- fast tier ----'] + fastl + ['', '# ---- final answers ----'] + impl_stable + mainl +
            last_blk + ['', '# ---- unsaved edits ----'] + edits + ['', '# ---- renames (last, see STRESS_RESULTS G-2) ----'] + renl)


# ---- project files ----------------------------------------------------------------------------------------------------------
MSTD = r'''#pragma once
// Minimal stand-in for the few standard-library facilities the generated code uses (no system headers needed).
namespace mstd {

typedef unsigned long size_t;

template <typename T>
class vector {
public:
    vector() : data_(0), size_(0), cap_(0) {}
    ~vector() { delete[] data_; }
    void push_back(const T& v) {
        if (size_ == cap_) grow();
        data_[size_++] = v;
    }
    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    void clear() { size_ = 0; }
    T& operator[](size_t i) { return data_[i]; }

private:
    vector(const vector&);
    vector& operator=(const vector&);
    void grow() {
        size_t n = cap_ ? cap_ * 2 : 4;
        T* d = new T[n];
        for (size_t i = 0; i < size_; ++i) d[i] = data_[i];
        delete[] data_;
        data_ = d;
        cap_ = n;
    }
    T* data_;
    size_t size_;
    size_t cap_;
};

template <typename A, typename B>
struct pair {
    A first;
    B second;
};

template <typename T>
T min(T a, T b) { return b < a ? b : a; }

template <typename T>
T max(T a, T b) { return a < b ? b : a; }

}  // namespace mstd
'''
MSTD_NAMES = ['mstd', 'size_t', 'vector', 'push_back', 'size', 'empty', 'clear', 'pair', 'first', 'second', 'min', 'max']


def guid(name):
    h = hashlib.md5(name.encode()).hexdigest().upper()
    return '{%s-%s-%s-%s-%s}' % (h[:8], h[8:12], h[12:16], h[16:20], h[20:32])


def inc_dirs(mod, g, app=False):
    d = ['app'] if app else ['modules/%s/include' % mod.name]
    for x in mod.closure:
        d.append('modules/%s/include' % x.name)
    if not app:
        d.append('modules/%s/src' % mod.name)
    d.append('mstd')
    return d


def tus(g):
    """(source path, module, language) for every translation unit."""
    out = []
    for m in g.mods + [g.app]:
        for p in m.srcs:
            out.append((p, m, 'c' if p.endswith('.c') else 'cxx'))
    return out


def write_cc(g, root):
    ents = []
    for p, m, lang in sorted(tus(g), key=lambda x: x[0]):
        ent = {'directory': '@CORPUS_ROOT@', 'file': '@CORPUS_ROOT@/' + p}
        args = ['clang' if lang == 'c' else 'clang++', '-std=c11' if lang == 'c' else '-std=c++17']
        args += ['-I@CORPUS_ROOT@/' + d for d in inc_dirs(m, g, m is g.app)]
        args += ['-D%s_BUILD=1' % re.sub('[^A-Za-z0-9]', '_', m.name).upper(), '-c', '@CORPUS_ROOT@/' + p]
        ent['arguments'] = args
        ents.append(ent)
    with open(os.path.join(root, 'compile_commands.json'), 'w') as fh:
        fh.write('[\n' + ',\n'.join(json.dumps(e, separators=(',', ':')) for e in ents) + '\n]\n')


VCX_HEAD = '''<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64">
      <Configuration>Debug</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64">
      <Configuration>Release</Configuration>
      <Platform>x64</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGuid>%(guid)s</ProjectGuid>
    <RootNamespace>%(name)s</RootNamespace>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.Default.props" />
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.props" />
  <ItemDefinitionGroup>
    <ClCompile>
      <LanguageStandard>stdcpp17</LanguageStandard>
      <AdditionalIncludeDirectories>%(incs)s%%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>%(defs)s%%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile>
      <PreprocessorDefinitions>_DEBUG;%%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <PreprocessorDefinitions>NDEBUG;%%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
  </ItemDefinitionGroup>
  <ItemGroup>
'''
VCX_TAIL = '''  </ItemGroup>
  <Import Project="$(VCTargetsPath)\\Microsoft.Cpp.targets" />
</Project>
'''


def write_vs(g, root):
    projs = []
    for m in g.mods + [g.app]:
        isapp = m is g.app
        base = 'app' if isapp else 'modules/%s' % m.name
        pth = '%s/%s.vcxproj' % (base, m.name)
        incs = ['$(ProjectDir)' + ('' if isapp else 'include')] if not isapp else ['$(ProjectDir)']
        incs = [d for d in incs if d.strip()]
        for x in m.closure:
            incs.append('$(SolutionDir)modules\\%s\\include' % x.name)
        incs.append('$(SolutionDir)mstd')
        if not isapp:
            incs.append('$(ProjectDir)src')
        defs = '%s_BUILD=1;' % re.sub('[^A-Za-z0-9]', '_', m.name).upper()
        body = VCX_HEAD % dict(guid=guid(m.name), name=m.name, incs=';'.join(incs) + ';', defs=defs)
        for p in sorted(m.srcs):
            body += '    <ClCompile Include="%s" />\n' % os.path.relpath(p, base).replace('/', '\\')
        hs = sorted(p for p in g.files if p.startswith(base + '/') and p.endswith('.h') and not p in m.srcs)
        for p in hs:
            body += '    <ClInclude Include="%s" />\n' % os.path.relpath(p, base).replace('/', '\\')
        body += VCX_TAIL
        os.makedirs(os.path.join(root, base), exist_ok=True)
        with open(os.path.join(root, pth), 'w', newline='\n') as fh:
            fh.write(body)
        projs.append((m.name, pth, guid(m.name)))
    s = ['Microsoft Visual Studio Solution File, Format Version 12.00', '# Visual Studio Version 17']
    for nm, pth, gd in projs:
        s += ['Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "%s", "%s", "%s"' % (nm, pth.replace('/', '\\'), gd), 'EndProject']
    s += ['Global', '\tGlobalSection(SolutionConfigurationPlatforms) = preSolution',
          '\t\tDebug|x64 = Debug|x64', '\t\tRelease|x64 = Release|x64', '\tEndGlobalSection',
          '\tGlobalSection(ProjectConfigurationPlatforms) = postSolution']
    for nm, pth, gd in projs:
        for cf in ('Debug', 'Release'):
            s.append('\t\t%s.%s|x64.ActiveCfg = %s|x64' % (gd, cf, cf))
            s.append('\t\t%s.%s|x64.Build.0 = %s|x64' % (gd, cf, cf))
    s += ['\tEndGlobalSection', 'EndGlobal']
    with open(os.path.join(root, 'gen%s.sln' % g.profile), 'w', newline='\n') as fh:
        fh.write('\n'.join(s) + '\n')


# ---- driver -----------------------------------------------------------------------------------------------------------------
def generate(profile, size, seed, variant, samples, impl_position='last'):
    g = Gen(profile, size, seed, variant, samples)
    g.table_fns = 12 if size <= 500 else (40 if size <= 5000 else 120)
    g.chain_depth = 5 if size <= 500 else (8 if size <= 5000 else 12)
    g.impl_position = impl_position
    g.edit_anchors = []
    g.final_text = {}
    for n in MSTD_NAMES:
        g.census[n] = g.census.get(n, 0) + 1
        g.census_ci[n.lower()] = g.census_ci.get(n.lower(), 0) + 1
    g.reserved.update(['gx', 'svc', 'lx', 'mx', 'app', 'main', 'mstd'])
    setup_modules(g)
    for m in g.mods:
        gen_module(g, m)
    gen_app(g)
    count_markers(g)
    choose_tracking(g)
    substitute(g)
    for p in sorted(g.files):
        g.final_text[p] = g.files[p].text()
    g.final_text['mstd/mstd.h'] = MSTD
    g.expect_lines = build_expect(g)
    return g


def write_all(g, out):
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(out)
    for p, t in g.final_text.items():
        fp = os.path.join(out, p)
        os.makedirs(os.path.dirname(fp), exist_ok=True)
        with open(fp, 'w', newline='\n') as fh:
            fh.write(t)
    if g.variant == 'cmake':
        write_cc(g, out)
    else:
        write_vs(g, out)
    with open(os.path.join(out, 'tenfold-expect.txt'), 'w', newline='\n') as fh:
        fh.write('\n'.join(g.expect_lines) + '\n')


def stats(g):
    loc = sum(t.count('\n') for t in g.final_text.values())
    files = len(g.final_text)
    kinds = {}
    for e in g.ents:
        kinds[e.kind] = kinds.get(e.kind, 0) + 1
    ek = {}
    for ln in g.expect_lines:
        w = ln.split()
        if not w or ln.startswith('#') or ln.startswith(' '):
            continue
        k = w[1] if w[0] in ('fast', 'xfail') and len(w) > 1 else w[0]
        if k in ('fast', 'xfail') and len(w) > 2:
            k = w[2]
        key = ('fast ' if ln.startswith('fast') or ln.startswith('xfail fast') else '') + k
        ek[key] = ek.get(key, 0) + 1
    return dict(profile=g.profile, size=g.size, seed=g.seed, variant=g.variant, samples=g.samples, files=files, loc=loc,
                modules=len(g.mods), tus=len(tus(g)), entities=kinds, tracked=sum(1 for e in g.ents if e.tracked),
                expectations=ek, expectation_lines=sum(ek.values()))


def check_syntax(g, out, jobs=4):
    """Compile every translation unit with clang -fsyntax-only (cmake-style flags); returns the list of failures."""
    import concurrent.futures
    cc = shutil.which('clang++') or shutil.which('g++')
    ccc = shutil.which('clang') or shutil.which('gcc')
    if not cc:
        return ['no C++ compiler']
    work = []
    for p, m, lang in sorted(tus(g), key=lambda x: x[0]):
        args = [ccc if lang == 'c' else cc, '-std=c11' if lang == 'c' else '-std=c++17', '-fsyntax-only', '-w']
        args += ['-I' + os.path.join(out, d) for d in inc_dirs(m, g, m is g.app)]
        args.append(os.path.join(out, p))
        work.append(args)
    fails = []

    def run(a):
        pr = subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True)
        return (a[-1], pr.stdout) if pr.returncode else None

    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for res in ex.map(run, work):
            if res:
                fails.append('%s\n%s' % (res[0], res[1][:1500]))
    # headers on their own (they must be self-contained)
    for p in sorted(g.final_text):
        if p.endswith('.h') and p.startswith('modules/'):
            m = [x for x in g.mods if p.startswith('modules/%s/' % x.name)][0]
            lang = 'c' if m.lang == 'c' else 'cxx'
            a = [ccc if lang == 'c' else cc, '-std=c11' if lang == 'c' else '-std=c++17', '-fsyntax-only', '-w', '-x',
                 'c-header' if lang == 'c' else 'c++-header']
            a += ['-I' + os.path.join(out, d) for d in inc_dirs(m, g, False)]
            a.append(os.path.join(out, p))
            fails_h = run(a)
            if fails_h:
                fails.append('HEADER ' + fails_h[0] + '\n' + fails_h[1])
    return fails


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out')
    ap.add_argument('--profile', default='game', choices=['game', 'service', 'lib', 'c', 'mixed'])
    ap.add_argument('--size', default='small')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--variant', default='cmake', choices=['cmake', 'vs'])
    ap.add_argument('--samples', type=int, default=None, help='tracked entities per category (default by size)')
    ap.add_argument('--impl-position', default='last', choices=['first', 'last'],
                    help='where the history-dependent `impl` lines go: first (deterministic, but every fast check then '
                         'runs after the TU is ready) or last (default)')
    ap.add_argument('--stats', help='write generation statistics (JSON) here')
    ap.add_argument('--check', action='store_true', help='compile every TU with clang -fsyntax-only')
    ap.add_argument('--jobs', type=int, default=4)
    a = ap.parse_args(argv)
    size = SIZES.get(a.size) or int(a.size)
    samples = a.samples if a.samples is not None else (6 if size <= 500 else 16 if size <= 5000 else 30)
    g = generate(a.profile, size, a.seed, a.variant, samples, a.impl_position)
    write_all(g, a.out)
    st = stats(g)
    if a.stats:
        with open(a.stats, 'w') as fh:
            json.dump(st, fh, indent=1, sort_keys=True)
    print(json.dumps({k: st[k] for k in ('profile', 'size', 'variant', 'files', 'loc', 'modules', 'tus', 'tracked',
                                         'expectation_lines')}, sort_keys=True))
    if a.check:
        fails = check_syntax(g, a.out, a.jobs)
        for f in fails[:10]:
            print(f, file=sys.stderr)
        print('check: %d failing translation units/headers' % len(fails))
        return 1 if fails else 0
    return 0


if __name__ == '__main__':
    sys.exit(main())
