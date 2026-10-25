#!/usr/bin/env python3
"""check_png.py FILE W H [HASH]: validate a PNG with Python's zlib (independent of our decoder).

Exit 0 if the signature, every chunk CRC, the zlib stream, the dimensions and (optionally) the
31-polynomial rolling hash of all RGBA bytes match. HASH uses the same fold as tests/ui/test_png.cpp.
"""
import struct
import sys
import zlib

path, w, h = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
data = open(path, "rb").read()
assert data[:8] == b"\x89PNG\r\n\x1a\n", "bad signature"
pos, idat, size = 8, b"", None
while pos < len(data):
    n, t = struct.unpack(">I4s", data[pos:pos + 8])
    body = data[pos + 8:pos + 8 + n]
    (crc,) = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])
    assert zlib.crc32(t + body) == crc, "bad crc in " + t.decode()
    if t == b"IHDR":
        size = struct.unpack(">II", body[:8])
        assert body[8:10] == b"\x08\x06", "not 8-bit RGBA"
    elif t == b"IDAT":
        idat += body
    pos += 12 + n
assert size == (w, h), "size %r != %r" % (size, (w, h))
raw = zlib.decompress(idat)
stride = w * 4 + 1
assert len(raw) == stride * h, "wrong decompressed length"
pixels = b"".join(raw[y * stride + 1:(y + 1) * stride] for y in range(h))
assert all(raw[y * stride] == 0 for y in range(h)), "unexpected filter"
if len(sys.argv) > 4:
    s = 0
    for b in pixels:
        s = (s * 31 + b) & 0xFFFFFFFF
    assert s == int(sys.argv[4]), "pixel hash mismatch"
print("ok", w, h)
