#!/usr/bin/env python3
"""Makes and checks the signed firmware manifest that the app reads.

Format: app/README.md, "Firmware manifest format". The signature is Ed25519 over
the UTF-8 bytes of "<version>\\n<size>\\n<sha256 lower-case hex>".

    # Once: make a key pair. Keep the private key out of the repository.
    python3 -I shared/tools/make_manifest.py keygen --out ~/onepage-manifest.key
    # It prints the base64 public key for FirmwareUpdater.manifestPublicKeyBase64.

    # Each release: sign the MCUboot image (build/firmware/zephyr/zephyr.signed.bin).
    python3 -I shared/tools/make_manifest.py sign --key ~/onepage-manifest.key \\
        --image zephyr.signed.bin --version 1.0.1 \\
        --url https://example.invalid/onepage-mini-1.0.1.bin --hw 1 --notes "Fixes" \\
        --out manifest.json

    # Check a manifest and image against a public key.
    python3 -I shared/tools/make_manifest.py verify --pub <base64> \\
        --manifest manifest.json --image zephyr.signed.bin

Needs the `cryptography` package.
"""
import argparse
import base64
import hashlib
import json
import sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)

# A fixed key for the app's unit test only. Never use it for a release.
TEST_SEED = bytes(range(32))


def signed_message(version: str, size: int, sha256_hex: str) -> bytes:
    return f"{version}\n{size}\n{sha256_hex.lower()}".encode()


def pub_b64(key: Ed25519PrivateKey) -> str:
    raw = key.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return base64.b64encode(raw).decode()


def build(key, image: bytes, version, url, hw=None, notes=None):
    sha = hashlib.sha256(image).hexdigest()
    sig = key.sign(signed_message(version, len(image), sha))
    m = {"version": version, "url": url, "size": len(image), "sha256": sha,
         "signature": base64.b64encode(sig).decode()}
    if hw:
        m["hw"] = hw
    if notes:
        m["notes"] = notes
    return m


def load_key(path):
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) != 32:
        sys.exit(f"{path}: expected a 32-byte raw Ed25519 seed")
    return Ed25519PrivateKey.from_private_bytes(raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    k = sub.add_parser("keygen")
    k.add_argument("--out", required=True)
    s = sub.add_parser("sign")
    s.add_argument("--key", required=True)
    s.add_argument("--image", required=True)
    s.add_argument("--version", required=True)
    s.add_argument("--url", required=True)
    s.add_argument("--hw", type=int, action="append")
    s.add_argument("--notes")
    s.add_argument("--out", required=True)
    v = sub.add_parser("verify")
    v.add_argument("--pub", required=True)
    v.add_argument("--manifest", required=True)
    v.add_argument("--image")
    t = sub.add_parser("test-fixture", help="writes the app test fixture (fixed test key)")
    t.add_argument("--out", required=True)
    a = ap.parse_args()

    if a.cmd == "keygen":
        key = Ed25519PrivateKey.generate()
        raw = key.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw,
                                serialization.NoEncryption())
        with open(a.out, "xb") as f:
            f.write(raw)
        print(pub_b64(key))
    elif a.cmd == "sign":
        with open(a.image, "rb") as f:
            image = f.read()
        m = build(load_key(a.key), image, a.version, a.url, a.hw, a.notes)
        with open(a.out, "w") as f:
            json.dump(m, f, indent=2)
            f.write("\n")
    elif a.cmd == "verify":
        with open(a.manifest) as f:
            m = json.load(f)
        pub = Ed25519PublicKey.from_public_bytes(base64.b64decode(a.pub))
        try:
            pub.verify(base64.b64decode(m["signature"]),
                       signed_message(m["version"], m["size"], m["sha256"]))
        except InvalidSignature:
            sys.exit("signature: FAIL")
        print("signature: OK")
        if a.image:
            with open(a.image, "rb") as f:
                image = f.read()
            ok = len(image) == m["size"] and hashlib.sha256(image).hexdigest() == m["sha256"].lower()
            print("image:", "OK" if ok else "FAIL")
            if not ok:
                sys.exit(1)
    elif a.cmd == "test-fixture":
        key = Ed25519PrivateKey.from_private_bytes(TEST_SEED)
        image = b"OnePage Mini test image\n" * 10
        m = build(key, image, "1.0.1", "https://example.invalid/fw.bin", [1], "Test")
        m["min_app"] = "1.0"  # an unknown key the app must ignore
        fixture = {"public_key": pub_b64(key), "image_hex": image.hex(), "manifest": m}
        with open(a.out, "w") as f:
            json.dump(fixture, f, indent=2)
            f.write("\n")


if __name__ == "__main__":
    main()
