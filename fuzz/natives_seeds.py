#!/usr/bin/env python3
"""Writes the seed corpus of fuzz/fuzz_natives.cpp (fuzz/seeds/natives): for every native wrapper and
property, inputs with well-formed arguments (valid key / nonce sizes, known hash and curve names), so
that fuzzing and the regular-toolchain replay (tests/CMakeLists.txt) start from the success paths.

    python3 fuzz/natives_seeds.py      # after adding or reordering operations in fuzz_natives.cpp
"""
import hashlib
import os
import re
import shutil
import struct

ROOT = os.path.dirname(os.path.abspath(__file__))
SRC = open(os.path.join(ROOT, "fuzz_natives.cpp"), encoding="utf-8").read()
OPS = [m.group(1) for m in re.finditer(r'\{"([fp]_\w+)", "\w*"\}', SRC)]
RSA_SPKI = bytes.fromhex("".join(re.findall(r'"([0-9a-f]+)"', SRC.split("const RSA_SPKI")[1].split(");")[0])))
HASH_SHA256, HASH_SHA512, HASH_SHA1, HASH_SHA3 = 3, 5, 1, 7  # indices into kHashNames
CURVES = {"P-256": (0, 32), "P-384": (1, 48), "P-521": (2, 66)}


def rnd(tag, n):
    out = b""
    i = 0
    while len(out) < n:
        out += hashlib.sha256(f"{tag}/{i}".encode()).digest()
        i += 1
    return out[:n]


def y(b):
    if len(b) < 240:
        return bytes([len(b)]) + b
    assert len(b) <= 4095
    return bytes([240 + len(b) // 256, len(b) % 256]) + b


def raw(s):  # a string argument given as raw bytes (dictionary kinds included)
    return b"\xe0" + y(s.encode() if isinstance(s, str) else s)


def d(i):  # a dictionary entry
    return bytes([i])


def n(v):
    if isinstance(v, int) and 0 <= v < 160:
        return bytes([v])
    if isinstance(v, int) and 0 <= v < 65536:
        return bytes([160, v >> 8, v & 255])
    return b"\xe0" + struct.pack("<d", float(v))


def op(name, *args):
    return bytes([OPS.index(name)]) + b"".join(args)


MSG = b"The quick brown fox jumps over the lazy dog"
PEM = b"-----BEGIN PUBLIC KEY-----\n" + __import__("base64").encodebytes(RSA_SPKI) + b"-----END PUBLIC KEY-----\n"

seeds = {
    "hex": op("f_hexEncode", y(MSG)) + op("f_hexDecode", raw(MSG.hex())),
    "base64": op("f_base64Encode", y(MSG)) + op("f_base64Decode", raw("VGhlIHF1aWNrIGJyb3du")),
    "base64url": op("f_base64UrlEncode", y(b"\xfb\xff")) + op("f_base64UrlDecode", raw("-_8")),
    "base32": op("f_base32Encode", y(b"foobar")) + op("f_base32Decode", raw("MZXW6YTBOI======")),
    "utf8": op("f_utf8Encode", raw("héllo \U0001f600")) + op("f_utf8Decode", y(b"\xc3\xa9\xed\xa0\x80\xff")),
    "pem": op("f_pemDecode", raw(PEM), d(1), n(1)) + op("f_pemDecode", raw(PEM), d(0), n(1))
    + op("f_pemEncode", y(RSA_SPKI[:64]), d(1)),
    "bytes": op("f_concatBytes", y(b"a"), y(b""), y(b"bc")) + op("f_bytesEqual", y(b"ab"), y(b"ab"))
    + op("f_timingSafeEqual", y(b"ab"), y(b"ac")) + op("f_crc32", y(MSG)) + op("f_randomBytes", n(16)),
    "digest": b"".join(op("f_digest", bytes([i]), y(MSG)) for i in range(8)),
    "hash": op("f_hash", d(HASH_SHA256), y(MSG)) + op("f_hash", d(11), y(b"abc")) + op("f_hash", raw("nope"), y(b"")),
    "hmac": op("f_hmac", d(HASH_SHA256), y(b"key"), y(MSG)) + op("p_hmacKey", d(HASH_SHA512), d(1), y(b"key"), y(MSG))
    + op("f_hmacKey", d(HASH_SHA1), d(1), y(MSG)) + op("f_hmacKey", d(HASH_SHA1), d(2), y(MSG)),
    "kdf": op("f_hkdf", d(HASH_SHA256), y(rnd("ikm", 22)), y(rnd("salt", 13)), y(b"info"), n(42))
    + op("f_pbkdf2", d(HASH_SHA256), y(b"password"), y(b"salt"), n(100), n(32))
    + op("p_kdfPrefix", d(HASH_SHA3), y(b"pw"), y(b"salt"), y(b"info"), bytes([40]), bytes([3])),
    "kdf-bad": op("f_hkdf", d(HASH_SHA256), y(b""), y(b""), y(b""), n(-1.5))
    + op("f_pbkdf2", d(HASH_SHA256), y(b"p"), y(b"s"), n(1e300), n(float("nan"))),
    "gcm": op("f_aesGcmEncrypt", y(rnd("k", 16)), y(rnd("iv", 12)), y(MSG), y(b"hdr"), n(1))
    + op("f_aesGcmDecrypt", y(rnd("k", 16)), y(rnd("iv", 12)), y(rnd("c", 40)), y(b""), n(0))
    + op("p_aead", n(0), y(rnd("k", 32)), y(rnd("iv", 12)), y(MSG), y(b"hdr"), n(1), bytes([77])),
    "chacha": op("f_chachaEncrypt", y(rnd("k", 32)), y(rnd("iv", 12)), y(MSG), y(b""), n(0))
    + op("f_chachaDecrypt", y(rnd("k", 32)), y(rnd("iv", 12)), y(rnd("c", 20)), y(b"a"), n(1))
    + op("p_aead", n(1), y(rnd("k", 32)), y(rnd("iv", 12)), y(MSG), y(b""), n(0), bytes([5])),
    "cbc": op("f_aesCbcEncrypt", y(rnd("k", 32)), y(rnd("iv", 16)), y(MSG))
    + op("f_aesCbcDecrypt", y(rnd("k", 32)), y(rnd("iv", 16)), y(rnd("c", 32)))
    + op("p_cbc", y(rnd("k", 24)), y(rnd("iv", 16)), y(MSG[:16])),
    "ctr": op("f_aesCtr", y(rnd("k", 16)), y(b"\xff" * 16), y(MSG)) + op("p_ctr", y(rnd("k", 32)), y(b"\xff" * 16), y(rnd("p", 100))),
    "ed25519": op("f_ed25519PublicKey", y(rnd("seed", 32))) + op("f_ed25519Sign", y(rnd("seed", 32)), y(MSG))
    + op("f_ed25519Verify", y(rnd("pub", 32)), y(MSG), y(rnd("sig", 64)))
    + op("p_ed25519", y(rnd("seed", 32)), y(MSG), bytes([200])),
    "x25519": op("f_x25519", y(rnd("a", 32)), y(rnd("b", 32))) + op("p_x25519", y(rnd("a", 32)), y(rnd("b", 32))),
    "rsa": op("f_rsaVerify", d(HASH_SHA256), y(RSA_SPKI), y(MSG), y(rnd("sig", 256)), d(0), n(1))
    + op("p_rsa", d(HASH_SHA256), y(MSG), d(0), n(1), bytes([9])) + op("p_rsa", d(HASH_SHA512), y(MSG), d(1), n(1), bytes([250])),
    "rsa-sign": op("f_rsaSign", d(HASH_SHA1), y(RSA_SPKI), y(MSG), d(0), n(0)),
    "encoding-props": op("p_encoding", y(MSG)) + op("p_encoding", y(rnd("e", 300))) + op("p_pem", y(rnd("der", 90)), d(2))
    + op("p_hash", y(MSG)),
}
for curve, (ci, size) in CURVES.items():
    priv = rnd("ec" + curve, size)
    if curve == "P-521":
        priv = b"\x00" + priv[1:]  # below the group order
    seeds["ecdsa-" + curve] = (op("f_ecdsaPublicKey", d(ci), y(priv)) + op("f_ecdsaSign", d(ci), d(HASH_SHA256), y(priv), y(MSG))
                               + op("f_ecdsaVerify", d(ci), d(HASH_SHA256), y(rnd("pub", 2 * size + 1)), y(MSG), y(rnd("s", 2 * size)))
                               + op("p_ecdsa", d(ci), d(HASH_SHA512), y(priv), y(MSG), bytes([31])))

out = os.path.join(ROOT, "seeds", "natives")
shutil.rmtree(out, ignore_errors=True)
os.makedirs(out)
for name, data in sorted(seeds.items()):
    with open(os.path.join(out, name), "wb") as f:
        f.write(data)
print(f"wrote {len(seeds)} seeds to {out} ({len(OPS)} operations)")
