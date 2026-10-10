"""Package blood_asset_author output as a Studio FBMOD and shared-bundle manifest."""
import argparse
import hashlib
import json
import struct
from pathlib import Path


def pack(fmt, *values):
    return struct.pack("<" + fmt, *values)


def terminated(value):
    return value.encode("utf-8") + b"\0"


def bundle_hash(name):
    value = 5381
    for byte in name.lower().encode("utf-8"):
        value = ((value * 33) ^ byte) & 0xffffffff
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("authored", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--mod-root", type=Path, help="Compiled Patch folder to receive shared-bundle metadata")
    args = parser.parse_args()
    source = json.loads((args.authored / "resources.json").read_text(encoding="utf-8"))
    rows = [pack("Bi", 0, -1) + terminated(name) + pack("I", 0)
            for name in ["Icon", "Screenshot0", "Screenshot1", "Screenshot2", "Screenshot3"]]
    payloads = []
    names = set()
    for asset in source["resources"]:
        identity = (asset["kind"], asset["name"])
        if identity in names:
            raise ValueError(f"Duplicate blood dependency: {identity}")
        names.add(identity)
        payload = (args.authored / asset["file"]).read_bytes()
        row = (pack("Bi", asset["kind"], len(payloads)) + terminated(asset["name"])
               + hashlib.sha1(payload).digest() + pack("qBi", asset["size"], 8 if asset["added"] else 0, 0)
               + terminated("") + pack("II", 1, bundle_hash(asset["target"])))
        if asset["kind"] == 2:
            meta = bytes.fromhex(asset["meta"])
            row += pack("IQI", asset["type"], asset["rid"], len(meta)) + meta
        elif asset["kind"] == 3:
            row += pack("IIIIiiI", 0, 0, asset["logical_offset"], asset["logical_size"], 0, -1, 0)
        rows.append(row)
        payloads.append(payload)
    profile = b"Skate"
    header = pack("QIQiB", 0x01005954534f5246, 6, 0, len(payloads), len(profile)) + profile + pack("I", 0)
    details = b"".join(terminated(s) for s in ["ReSkate Blood", "ReSkate", "Gameplay", "1.0.0",
                       "Dynamic impact spray, droplets, wound streams, and ground blood smears.", ""])
    body = header + details + pack("I", len(rows)) + b"".join(rows)
    body = body[:12] + pack("Q", len(body)) + body[20:]
    offset = 0
    manifest = b""
    for payload in payloads:
        manifest += pack("QQ", offset, len(payload))
        offset += len(payload)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(body + manifest + b"".join(payloads))
    print(f"Packaged {len(payloads)} blood payloads: {args.output}")
    if args.mod_root:
        args.mod_root.mkdir(parents=True, exist_ok=True)
        def save(name, value):
            (args.mod_root / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
        save("reskate-shared-bundles.json", {"schema": 1, "bundles": [source["target"]]})
        save("manifest.json", {"name": "ReSkate Blood", "author": "ReSkate", "version_number": "1.0.0",
             "description": "Dynamic impact spray, droplets, wound streams, and ground smears for ReSkate."})


if __name__ == "__main__":
    main()
