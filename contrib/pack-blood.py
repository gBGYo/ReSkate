"""Build a launcher-importable Blood release from compiled assets and Release binaries."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import zipfile

REPO = Path(__file__).resolve().parents[1]
def sha(data):
    return hashlib.sha256(data).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default="1.0.0")
    parser.add_argument("--build", type=Path, default=REPO / "build/vs2022-x64")
    parser.add_argument("--assets", type=Path, default=REPO / "artifacts/Blood/Patch")
    parser.add_argument("--output", type=Path, default=REPO / "artifacts")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9]+[.][0-9]+[.][0-9]+", args.version):
        parser.error("version must be major.minor.patch")
    cache = (args.build / "CMakeCache.txt").read_text(encoding="utf-8")
    for setting in [f"DINGOSDK_VERSION:STRING={args.version}-blood", "DINGOSDK_LAUNCHER_AUTO_UPDATE:BOOL=OFF"]:
        if setting not in cache.splitlines():
            raise RuntimeError(f"Reconfigure and rebuild Release with {setting}")
    if subprocess.check_output(["git", "status", "--porcelain"], cwd=REPO, text=True).strip():
        raise RuntimeError("Commit source changes before packaging so Source.zip matches the release.")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=REPO, text=True).strip()
    identity = (REPO / "Engine/Game/Build/supported_build.h").read_text(encoding="utf-8")
    game_hash = re.search(r'game_sha256\s*=\s*"([a-f0-9]{64})"', identity)[1]
    game_build = re.search(r'steam_build_id\s*=\s*"([0-9]+)"', identity)[1]
    for name in ["layout.toc", "reskate-shared-bundles.json", "Win32"]:
        if not (args.assets / name).exists():
            raise RuntimeError(f"Missing compiled blood asset: {name}")
    files = {p.relative_to(args.assets).as_posix(): p.read_bytes()
             for p in args.assets.rglob("*") if p.is_file() and not p.name.startswith(".") and p.name != "manifest.json"}
    asset_hashes = {name: sha(data) for name, data in files.items()}
    for p in (REPO / "packaging/Blood").iterdir():
        if p.is_file():
            files[p.name] = p.read_bytes()
    binaries = {}
    for name in ["ReSkate.dll", "ReSkateLauncher.exe"]:
        binary = args.build / "Release" / name
        # Check the actual binary's version, not just the CMake cache.
        actual = subprocess.check_output(["powershell.exe", "-NoProfile", "-Command",
            "(Get-Item -LiteralPath '" + str(binary.resolve()).replace("'", "''") + "').VersionInfo.ProductVersion"], text=True).strip()
        if actual != args.version + "-blood":
            raise RuntimeError(f"Rebuild {name}: version is {actual!r}")
        files[name] = binary.read_bytes()
        binaries[name] = {"sha256": sha(files[name]), "bytes": len(files[name])}
    files["icon.png"] = (REPO / "assets/launcher/icon_mods.png").read_bytes()
    files["LICENSE"] = (REPO / "LICENSE").read_bytes()
    files["THIRD-PARTY-NOTICES.md"] = (REPO / "External/README.md").read_bytes()
    for p in (REPO / "External").rglob("*"):
        if p.is_file() and any(word in p.name.lower() for word in ["license", "licence", "copying", "ofl"]):
            files["licenses/" + p.relative_to(REPO / "External").as_posix()] = p.read_bytes()
    files["Source.zip"] = subprocess.check_output(["git", "archive", "--format=zip", "--prefix=ReSkate-Blood/", revision], cwd=REPO)
    files["BUILD.txt"] = (f"Source revision: {revision}\n"
        f"cmake --preset vs2022-x64 -DDINGOSDK_VERSION={args.version}-blood -DDINGOSDK_LAUNCHER_AUTO_UPDATE=OFF -DDINGOSDK_BUILD_BLOOD_TESTS=ON\n"
        "cmake --build build/vs2022-x64 --config Release --target dingosdk_runtime dingosdk_launcher dingosdk_blood_asset_author --parallel 4\n"
        "See Extension/Blood/README.md in Source.zip for asset authoring and regression tests.\n").encode()
    files["manifest.json"] = json.dumps({"name": "ReSkate_Blood", "author": "ReSkate", "version_number": args.version,
        "website_url": "", "description": "Standalone blood spray, persistent trails and smears. Includes the required runtime and backup/restore installer.",
        "dependencies": []}, indent=2).encode() + b"\n"
    files["build-info.json"] = json.dumps({"schema": 1, "name": "ReSkate_Blood", "version": args.version,
        "source_revision": revision, "supported_game": {"build_id": game_build, "sha256": game_hash},
        "build": {"configuration": "Release", "architecture": "x64", "binary_updates": False},
        "binaries": binaries, "assets": asset_hashes}, indent=2).encode() + b"\n"
    args.output.mkdir(parents=True, exist_ok=True)
    output = args.output / f"ReSkate_Blood-{args.version}.zip"
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(output) as archive:
        if archive.testzip() is not None:
            raise RuntimeError("ZIP integrity check failed")
    output.with_suffix(".zip.sha256").write_text(f"{sha(output.read_bytes())}  {output.name}\n", encoding="ascii")
    print(f"Package: {output} ({output.stat().st_size / 1024**2:.1f} MiB; {len(files)} files)")

if __name__ == "__main__":
    main()
