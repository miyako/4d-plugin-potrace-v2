#!/usr/bin/env python3
"""Potrace's exact-path macOS release operations; see native.md for workflow wiring."""

import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

import source_version


BUNDLE = Path("test/Plugins/Potrace.bundle")
CONTENTS = BUNDLE / "Contents"
BINARY = CONTENTS / "MacOS/Potrace"
WINDOWS = CONTENTS / "Windows64/Potrace.4DX"


def safe(path):
    """Reject links in paths we mutate, without traversing preserved resources."""
    path = Path(path)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"Unsafe relative path: {path}")
    for candidate in (*reversed(path.parents), path):
        if candidate.is_symlink():
            raise ValueError(f"Symlink in native output path: {candidate}")
    return path


def remove(path):
    path = safe(path)
    if path.is_dir():
        # Do not let a stale build tree redirect cleanup outside this checkout.
        for root, directories, files in os.walk(path, followlinks=False):
            for name in directories + files:
                safe(Path(root) / name)
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()
    if path.exists() or path.is_symlink():
        raise RuntimeError(f"Native output survived cleanup: {path}")


def require(path):
    path = safe(path)
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"Missing or empty exact output: {path}")
    return path


def run(*args):
    return subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE).stdout


def export(**values):
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as stream:
        for key, value in values.items():
            if "\n" in str(value) or "\r" in str(value):
                raise ValueError("Invalid environment value")
            stream.write(f"{key}={value}\n")


def verify():
    require(BINARY)
    archs = run("lipo", "-archs", str(BINARY)).split()
    if sorted(archs) != ["arm64", "x86_64"]:
        raise ValueError(f"Expected exactly arm64 + x86_64, got {archs}")
    run("lipo", "-verify_arch", "arm64", "x86_64", str(BINARY))
    require(CONTENTS / "Info.plist")
    require(CONTENTS / "Resources/English.lproj/InfoPlist.strings")
    source_version.verify_bundle(BUNDLE)


def build():
    source_version.read()
    # Xcode regenerates Info.plist; the committed manifest/constants/thumbnail
    # are NOT in its Resources build phase and must remain in the bundle.
    for path in (
        CONTENTS / "MacOS", CONTENTS / "_CodeSignature",
        CONTENTS / "CodeResources", CONTENTS / "Info.plist",
        Path("test/Plugins/Default"), Path("test/Plugins/Potrace.build"),
    ):
        remove(path)
    run(
        "xcodebuild", "-project", "Potrace.xcodeproj", "-target", "Potrace",
        "-configuration", "Default", "ARCHS=arm64 x86_64", "ONLY_ACTIVE_ARCH=NO",
        "MACOSX_DEPLOYMENT_TARGET=11.0", "CODE_SIGNING_ALLOWED=NO",
        f"CONFIGURATION_BUILD_DIR={Path.cwd() / 'test/Plugins'}", "build",
    )
    verify()


def merge():
    source = require("winbuild/Potrace.4DX")
    destination = safe(WINDOWS)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    require(destination)
    export(BUNDLE_PATH=BUNDLE)


def package(version):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
        raise ValueError("Expected a numeric MAJOR.MINOR.PATCH package version")
    if source_version.read() != version:
        raise ValueError("Package version differs from selected source")
    verify()
    require(WINDOWS)
    zip_path = Path(f"Potrace-{version}.zip")
    dmg_path = Path(f"Potrace-{version}.dmg")
    for path in (zip_path, dmg_path):
        remove(path)
    run("ditto", "-c", "-k", "--keepParent", str(BUNDLE), str(zip_path))
    run("hdiutil", "create", "-format", "UDBZ", "-volname", "Potrace",
        "-srcfolder", str(BUNDLE), str(dmg_path))
    credentials = ("--keychain-profile", "notarytool-profile",
                   "--keychain", os.environ["KEYCHAIN_PATH"])
    for path in (zip_path, dmg_path):
        require(path)
        result = json.loads(run("xcrun", "notarytool", "submit", str(path),
                                *credentials, "--wait", "--output-format", "json"))
        if not isinstance(result, dict) or result.get("status") != "Accepted":
            raise ValueError(f"Notarization not Accepted for {path}")
    run("xcrun", "stapler", "staple", str(dmg_path))
    export(ZIP_PATH=zip_path, DMG_PATH=dmg_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("mac-build", "verify", "merge", "package"))
    parser.add_argument("--version")
    args = parser.parse_args()
    if args.action == "package":
        if not args.version:
            parser.error("package requires --version")
        package(args.version)
    else:
        {"mac-build": build, "verify": verify, "merge": merge}[args.action]()


if __name__ == "__main__":
    main()
