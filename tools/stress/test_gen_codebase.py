#!/usr/bin/env python3
"""Quick tests of the generator (W9g): determinism, anchor hygiene, project files.

    python3 -m unittest discover -s tools/stress -p 'test_gen_codebase.py'      (about 20 s)
    TENFOLD_GEN_CHECK=1 ...    additionally compiles every translation unit with clang -fsyntax-only (needs clang++)
"""
import json
import os
import re
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_codebase as gc  # noqa: E402

ANCHOR = re.compile(r'/\*@([A-Za-z0-9_.]+)\*/')
KINDS = {'def', 'decl', 'typedef', 'impl', 'refs', 'hover', 'sym', 'rename', 'open', 'edit', 'config'}


def tree(root):
    out = {}
    for d, _, fs in os.walk(root):
        for f in fs:
            p = os.path.join(d, f)
            with open(p, 'rb') as fh:
                out[os.path.relpath(p, root)] = fh.read()
    return out


class GenTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix='tf-gen-test-')

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def gen(self, name, profile='game', size=80, seed=1, variant='cmake', samples=3):
        out = os.path.join(self.tmp, name)
        g = gc.generate(profile, size, seed, variant, samples)
        gc.write_all(g, out)
        return g, out

    def test_deterministic(self):
        for profile in ('game', 'mixed', 'c'):
            _, a = self.gen('a', profile, seed=7)
            _, b = self.gen('b', profile, seed=7)
            self.assertEqual(tree(a), tree(b), profile)
            _, c = self.gen('c', profile, seed=8)
            self.assertNotEqual(tree(a), tree(c), profile)

    def test_anchors_and_expectations(self):
        for profile in ('game', 'service', 'lib', 'c', 'mixed'):
            g, out = self.gen('p', profile)
            seen = {}
            for rel, data in tree(out).items():
                if rel in ('tenfold-expect.txt', 'compile_commands.json') or rel.endswith(('.sln', '.vcxproj')):
                    continue
                for m in ANCHOR.finditer(data.decode()):
                    self.assertNotIn(m.group(1), seen, 'duplicate anchor %s' % m.group(1))
                    seen[m.group(1)] = rel
            with open(os.path.join(out, 'tenfold-expect.txt')) as fh:
                lines = fh.read().split('\n')
            for ln in lines:
                if ln.startswith('edit '):
                    for a in ANCHOR.findall(ln):
                        seen[a] = 'edit'
            n = 0
            for ln in lines:
                if not ln.strip() or ln.startswith('#') or ln.startswith(' '):
                    continue
                words = ln.split('#')[0].split()
                while words and words[0] in ('fast', 'xfail'):
                    words = words[1:]
                self.assertIn(words[0], KINDS, ln)
                if words[0] in ('open',):
                    continue
                n += 1
                text = re.sub(r'"[^"]*"', '', ln.split('#')[0])
                if words[0] == 'edit':
                    text = ' '.join(words[:2])
                    inserted = set(ANCHOR.findall(ln))
                else:
                    inserted = set()
                for tok in re.findall(r'[A-Za-z0-9_.]+', text):
                    if tok in seen or tok in inserted:
                        continue
                    # words, keywords, quoted queries and numbers are not anchors; anchors always contain `_`+digit
                    if re.search(r'_\d+_[a-z]+\d+$|^ed\d+_use$|_\d+_t', tok):
                        self.fail('unknown anchor %s in: %s' % (tok, ln))
            self.assertGreater(n, 30, profile)

    def test_variants(self):
        g, out = self.gen('cmake', 'service', variant='cmake')
        with open(os.path.join(out, 'compile_commands.json')) as fh:
            cc = json.load(fh)
        self.assertEqual(len(cc), len(gc.tus(g)))
        self.assertTrue(all('@CORPUS_ROOT@' in e['file'] for e in cc))
        g, out = self.gen('vs', 'service', variant='vs')
        slns = [f for f in os.listdir(out) if f.endswith('.sln')]
        self.assertEqual(len(slns), 1)
        self.assertFalse(os.path.exists(os.path.join(out, 'compile_commands.json')))
        n = 0
        for d, _, fs in os.walk(out):
            for f in fs:
                if f.endswith('.vcxproj'):
                    with open(os.path.join(d, f)) as fh:
                        n += fh.read().count('<ClCompile Include=')
        self.assertEqual(n, len(gc.tus(g)))

    @unittest.skipUnless(os.environ.get('TENFOLD_GEN_CHECK') and shutil.which('clang++'), 'set TENFOLD_GEN_CHECK=1')
    def test_compiles(self):
        for profile in ('game', 'lib', 'c', 'mixed'):
            g, out = self.gen('chk', profile, size=120)
            self.assertEqual(gc.check_syntax(g, out, 4), [], profile)


if __name__ == '__main__':
    unittest.main()
