"""Closed-PATH native boundary tests: no native build/sign/notary/4D runs."""

import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[2]
SCRIPTS = ROOT / ".github/scripts"
PWSH = shutil.which("pwsh")
sys.path.insert(0, str(SCRIPTS))
SPEC = importlib.util.spec_from_file_location("native", SCRIPTS / "native.py")
native = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(native)


STUB = r'''
import json, os, plistlib, struct, sys
from pathlib import Path
tool = Path(sys.argv[0]).name
args = sys.argv[1:]
with open("calls.jsonl", "a") as log:
    log.write(json.dumps([tool, *args]) + "\n")
mode = os.environ.get("MODE", "")
version = os.environ["SOURCE_VERSION"]
stale = os.environ["STALE_VERSION"]
contents = Path("test/Plugins/Potrace.bundle/Contents")
binary = contents / "MacOS/Potrace"
if tool == "xcodebuild":
    assert args == [
        "-project", "Potrace.xcodeproj", "-target", "Potrace",
        "-configuration", "Default", "ARCHS=arm64 x86_64", "ONLY_ACTIVE_ARCH=NO",
        "MACOSX_DEPLOYMENT_TARGET=11.0", "CODE_SIGNING_ALLOWED=NO",
        "CONFIGURATION_BUILD_DIR=" + str(Path.cwd() / "test/Plugins"), "build"]
    for path in [contents / "MacOS", contents / "_CodeSignature",
                 contents / "Info.plist", contents / "CodeResources",
                 Path("test/Plugins/Default"), Path("test/Plugins/Potrace.build")]:
        assert not path.exists(), "stale output: " + str(path)
    assert (contents / "Windows64/Potrace.4DX").read_bytes() == b"committed windows"
    if mode == "build-fail": sys.exit(41)
    if mode != "missing-binary":
        binary.parent.mkdir(parents=True)
        binary.write_bytes(b"rebuilt universal")
    if mode != "missing-plist":
        data = plistlib.loads(Path("Info.plist").read_bytes())
        if mode == "stale-plist": data["CFBundleShortVersionString"] = stale
        (contents / "Info.plist").write_bytes(plistlib.dumps(data))
    localized = contents / "Resources/English.lproj/InfoPlist.strings"
    if mode == "missing-localized":
        localized.unlink()
    else:
        data = Path("English.lproj/InfoPlist.strings").read_bytes()
        if mode == "stale-localized": data = data.replace(version.encode(), stale.encode())
        localized.write_bytes(data)
elif tool == "lipo":
    assert args in [
        ["-archs", str(binary)], ["-verify_arch", "arm64", "x86_64", str(binary)]]
    if args[0] == "-archs":
        print(os.environ.get("ARCHS", "arm64 x86_64"))
    elif mode == "lipo-fail": sys.exit(42)
elif tool == "msbuild":
    assert args == ["Potrace.sln", "/p:Configuration=Release", "/p:Platform=x64",
                    "/p:PlatformToolset=v143", "/p:WindowsTargetPlatformVersion=10.0", "/m"]
    source = Path("test/plugins/Potrace.bundle/contents/windows64/Potrace.4DX")
    assert not source.exists(), "stale Windows output"
    assert not Path("Objs").exists()
    assert not Path("test/Plugins/Default").exists()
    if mode == "build-fail": sys.exit(43)
    if mode != "missing-binary":
        data = bytearray(256)
        data[:2] = b"MZ"
        struct.pack_into("<I", data, 0x3c, 128)
        struct.pack_into("<I", data, 128, 0x4550)
        struct.pack_into("<H", data, 132, 0x14c if mode == "x86" else 0x8664)
        struct.pack_into("<H", data, 152, 0x20b)
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(data)
elif tool in ("ditto", "hdiutil"):
    expected = (["-c", "-k", "--keepParent", "test/Plugins/Potrace.bundle"]
                if tool == "ditto" else ["create", "-format", "UDBZ", "-volname",
                "Potrace", "-srcfolder", "test/Plugins/Potrace.bundle"])
    assert args[:-1] == expected
    assert not Path(args[-1]).exists(), "stale archive"
    if mode == tool + "-fail": sys.exit(44)
    Path(args[-1]).write_bytes(b"rebuilt archive")
elif tool == "xcrun":
    if args[:2] == ["notarytool", "submit"]:
        assert args[3:] == ["--keychain-profile", "notarytool-profile", "--keychain",
                            "fixture.keychain", "--wait", "--output-format", "json"]
        second = args[2].endswith(".dmg")
        if mode == "transport" or (mode == "second-transport" and second): sys.exit(45)
        if mode == "malformed": print("not JSON")
        elif mode == "array": print("[]")
        elif mode == "missing-status": print("{}")
        elif mode == "null-status": print('{"status": null}')
        else:
            status = "Invalid" if mode == "invalid" or (
                mode == "second-invalid" and second) else (
                "In Progress" if mode == "pending" else (
                    "accepted" if mode == "lowercase" else "Accepted"))
            print(json.dumps({"id": "fixture", "status": status}))
    else:
        assert args == ["stapler", "staple", f"Potrace-{version}.dmg"]
        if mode == "staple-fail": sys.exit(46)
else:
    raise AssertionError("Unexpected native command: " + tool)
'''


class Fixture(unittest.TestCase):
    def setUp(self):
        self.version = native.source_version.read(ROOT)
        major, minor, patch_version = map(int, self.version.split("."))
        self.stale_version = f"{major}.{minor}.{patch_version + 1}"
        self.directory = tempfile.TemporaryDirectory(prefix=".native-tests-", dir=ROOT)
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.bin = self.root / "stub-bin"
        self.bin.mkdir()
        for tool in ("msbuild", "xcodebuild", "lipo", "ditto", "hdiutil", "xcrun",
                     "codesign", "security", "4D"):
            path = self.bin / tool
            path.write_text(f"#!{sys.executable}\n" + STUB)
            path.chmod(0o755)
        self.env = {
            "PATH": str(self.bin), "PYTHONDONTWRITEBYTECODE": "1",
            "GITHUB_ENV": str(self.root / "github-env"),
            "KEYCHAIN_PATH": "fixture.keychain",
            "SOURCE_VERSION": self.version, "STALE_VERSION": self.stale_version,
        }
        for relative in native.source_version.SOURCE_PATHS:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, path)
        self.contents = self.root / native.CONTENTS
        self.preserved_bytes = {}
        for relative in (
            "QuickLook/Thumbnail.png", "Resources/constants.xlf", "Resources/manifest.json",
        ):
            data = (ROOT / native.CONTENTS / relative).read_bytes()
            self.write(relative, data)
            self.preserved_bytes[relative] = data
        self.write("Windows64/Potrace.4DX", b"committed windows")
        self.write("Resources/English.lproj/InfoPlist.strings", (
            ROOT / native.CONTENTS / "Resources/English.lproj/InfoPlist.strings").read_bytes())
        for relative in ("MacOS/Potrace", "_CodeSignature/CodeResources",
                         "CodeResources", "Info.plist"):
            self.write(relative, b"stale")
        for relative in ("test/Plugins/Default/stale", "test/Plugins/Potrace.build/stale",
                         "Objs/Release/stale"):
            self.put(relative, b"stale")
        self.put("winbuild/Potrace.4DX", b"downloaded windows")

    def put(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def write(self, relative, data):
        return self.put(native.CONTENTS / relative, data)

    def calls(self):
        log = self.root / "calls.jsonl"
        return [json.loads(line) for line in log.read_text().splitlines()] if log.exists() else []

    def preserved(self):
        for relative, data in self.preserved_bytes.items():
            self.assertEqual((self.contents / relative).read_bytes(), data, relative)

    def run_python(self, action, **env):
        return subprocess.run(
            [sys.executable, "-B", str(SCRIPTS / "native.py"), action,
             *(["--version", self.version] if action == "package" else [])],
            cwd=self.root, env={**self.env, **env}, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        )

    def ok(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class MacTests(Fixture):
    def test_build_invalidates_native_outputs_preserves_resources_and_windows(self):
        self.ok(self.run_python("mac-build"))
        self.preserved()
        self.assertEqual((self.contents / "Windows64/Potrace.4DX").read_bytes(),
                         b"committed windows")
        self.assertEqual((self.contents / "MacOS/Potrace").read_bytes(), b"rebuilt universal")
        self.assertEqual(
            (self.contents / "Resources/English.lproj/InfoPlist.strings").read_bytes(),
            (self.root / "English.lproj/InfoPlist.strings").read_bytes())
        self.assertEqual([call[0] for call in self.calls()], ["xcodebuild", "lipo", "lipo"])
        self.ok(self.run_python("mac-build"))
        self.preserved()

    def test_incomplete_or_failed_build_is_rejected(self):
        for mode in ("missing-binary", "missing-plist", "stale-plist", "missing-localized",
                     "stale-localized", "build-fail", "lipo-fail"):
            with self.subTest(mode=mode):
                result = self.run_python("mac-build", MODE=mode)
                self.assertNotEqual(result.returncode, 0)
                self.preserved()

    def test_source_drift_aborts_before_cleanup_or_build(self):
        path = self.root / "English.lproj/InfoPlist.strings"
        path.write_bytes(path.read_bytes().replace(
            self.version.encode(), self.stale_version.encode()))
        self.assertNotEqual(self.run_python("mac-build").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual((self.contents / "MacOS/Potrace").read_bytes(), b"stale")

    def test_explicit_source_verifier_cli_before_sign(self):
        self.ok(self.run_python("mac-build"))

        def verify():
            return subprocess.run(
                [sys.executable, "-B", str(SCRIPTS / "source_version.py"),
                 "--verify-bundle", str(native.BUNDLE)],
                cwd=self.root, env=self.env, text=True,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
            )

        calls = self.calls()
        self.ok(verify())
        localized = self.contents / "Resources/English.lproj/InfoPlist.strings"
        localized.write_bytes(localized.read_bytes().replace(
            self.version.encode(), self.stale_version.encode()))
        self.assertNotEqual(verify().returncode, 0)
        self.assertEqual(self.calls(), calls)

    def test_bundle_source_check_follows_universal_verification(self):
        old = Path.cwd()
        try:
            os.chdir(self.root)
            with patch.object(native, "run", side_effect=["arm64 x86_64", ""]), \
                    patch.object(native.source_version, "verify_bundle") as check:
                native.verify()
                check.assert_called_once_with(native.BUNDLE)
            with patch.object(native, "run", return_value="x86_64"), \
                    patch.object(native.source_version, "verify_bundle") as check:
                with self.assertRaises(ValueError):
                    native.verify()
                check.assert_not_called()
        finally:
            os.chdir(old)

    def test_wrong_architectures_fail(self):
        for archs in ("arm64", "x86_64", "", "arm64 x86_64 i386", "arm64 arm64"):
            with self.subTest(archs=archs):
                self.assertNotEqual(self.run_python("mac-build", ARCHS=archs).returncode, 0)

    def test_preserved_framework_symlink_is_not_cleaned(self):
        self.write("Frameworks/F.framework/Versions/A/F", b"dependency")
        path = self.contents / "Frameworks/F.framework/Versions/Current"
        path.symlink_to("A")
        self.ok(self.run_python("mac-build"))
        self.assertTrue(path.is_symlink())
        self.assertEqual((path / "F").read_bytes(), b"dependency")

    def test_cleanup_symlink_fails_before_build(self):
        shutil.rmtree(self.contents / "MacOS")
        protected = self.put("protected/Potrace", b"protected")
        (self.contents / "MacOS").symlink_to(protected.parent, target_is_directory=True)
        self.assertNotEqual(self.run_python("mac-build").returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual(protected.read_bytes(), b"protected")

    def test_cleanup_error_aborts_no_native_call(self):
        old = Path.cwd()
        try:
            os.chdir(self.root)
            with patch.object(native.shutil, "rmtree", side_effect=PermissionError("denied")), \
                    patch.object(native, "run") as run:
                with self.assertRaises(PermissionError):
                    native.build()
                run.assert_not_called()
        finally:
            os.chdir(old)

    def test_exact_merge_ignores_nested_stale_match(self):
        self.put("winbuild/nested/Potrace.4DX", b"wrong")
        self.ok(self.run_python("merge"))
        self.assertEqual((self.root / native.WINDOWS).read_bytes(), b"downloaded windows")
        self.assertEqual((self.root / "github-env").read_text(),
                         "BUNDLE_PATH=test/Plugins/Potrace.bundle\n")
        self.preserved()

    def test_missing_exact_artifact_does_not_fall_back(self):
        (self.root / "winbuild/Potrace.4DX").unlink()
        self.put("winbuild/nested/Potrace.4DX", b"wrong")
        self.assertNotEqual(self.run_python("merge").returncode, 0)
        self.assertFalse((self.root / "github-env").exists())
        self.assertEqual((self.root / native.WINDOWS).read_bytes(), b"committed windows")

    def test_merge_rejects_linked_artifact_or_destination(self):
        source = self.root / "winbuild/Potrace.4DX"
        source.unlink()
        source.symlink_to(self.put("elsewhere/Potrace.4DX", b"wrong"))
        self.assertNotEqual(self.run_python("merge").returncode, 0)
        source.unlink()
        source.write_bytes(b"valid")
        destination = self.root / native.WINDOWS
        destination.unlink()
        destination.symlink_to(self.put("protected/windows", b"protected"))
        self.assertNotEqual(self.run_python("merge").returncode, 0)
        self.assertEqual((self.root / "protected/windows").read_bytes(), b"protected")

    def test_notary_requires_both_accepted_before_staple_and_export(self):
        self.ok(self.run_python("mac-build"))
        for mode in ("transport", "second-transport", "malformed", "array",
                     "missing-status", "null-status", "invalid", "second-invalid",
                     "pending", "lowercase", "ditto-fail", "hdiutil-fail", "staple-fail"):
            with self.subTest(mode=mode):
                (self.root / "calls.jsonl").unlink()
                for ext in ("zip", "dmg"):
                    self.put(f"Potrace-{self.version}.{ext}", b"stale archive")
                result = self.run_python("package", MODE=mode)
                self.assertNotEqual(result.returncode, 0, mode)
                self.assertFalse((self.root / "github-env").exists())
                stapled = any(c[:3] == ["xcrun", "stapler", "staple"] for c in self.calls())
                self.assertEqual(stapled, mode == "staple-fail")
        (self.root / "calls.jsonl").unlink()
        self.ok(self.run_python("package"))
        calls = self.calls()
        self.assertEqual([c[2] for c in calls if c[:2] == ["xcrun", "notarytool"]],
                         ["submit", "submit"])
        self.assertEqual(calls[-1], [
            "xcrun", "stapler", "staple", f"Potrace-{self.version}.dmg"])
        self.assertEqual((self.root / "github-env").read_text(),
                         f"ZIP_PATH=Potrace-{self.version}.zip\n"
                         f"DMG_PATH=Potrace-{self.version}.dmg\n")


@unittest.skipUnless(PWSH, "pwsh unavailable: Windows helpers not executed")
class WindowsTests(Fixture):
    def run_ps(self, action="build", prelude="", **env):
        return subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
             prelude + "\n& '" + str(SCRIPTS / "native-windows.ps1") + "' -Action " + action],
            cwd=self.root, env={**self.env, **env}, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30,
        )

    def test_exact_msbuild_output_architecture_and_resources(self):
        self.put("test/aaa-stale/Potrace.4DX", b"wrong recursive match")
        mac = (self.contents / "MacOS/Potrace").read_bytes()
        self.ok(self.run_ps())
        data = (self.root / native.WINDOWS).read_bytes()
        self.assertEqual(struct.unpack_from("<H", data, 132)[0], 0x8664)
        source = self.root / "test/plugins/Potrace.bundle/contents/windows64/Potrace.4DX"
        self.assertEqual(data, source.read_bytes())
        self.assertEqual([c[0] for c in self.calls()], ["msbuild"])
        self.assertEqual((self.contents / "MacOS/Potrace").read_bytes(), mac)
        self.preserved()
        # Also covers same-file normalization on case-insensitive hosts.
        self.ok(self.run_ps("normalize"))
        self.preserved()

    def test_build_failure_missing_or_x86_output_never_normalizes(self):
        for mode in ("build-fail", "missing-binary", "x86"):
            with self.subTest(mode=mode):
                result = self.run_ps(MODE=mode)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.preserved()
                if mode != "x86":
                    self.assertFalse((self.root / native.WINDOWS).exists())

    def test_clean_missing_is_idempotent(self):
        self.ok(self.run_ps("clean"))
        self.ok(self.run_ps("clean"))
        self.assertEqual(self.calls(), [])
        self.assertFalse((self.root / native.WINDOWS).exists())
        self.preserved()

    def test_clean_error_prevents_build(self):
        mock = r'''
function Remove-Item {
    [CmdletBinding()]
    param([string]$LiteralPath, [switch]$Recurse, [switch]$Force)
    throw "mock removal denied"
}
'''
        result = self.run_ps(prelude=mock)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("mock removal denied", result.stderr)
        self.assertEqual(self.calls(), [])
        self.preserved()

    def test_reparse_cleanup_rejected(self):
        shutil.rmtree(self.root / "Objs")
        protected = self.put("protected/object", b"protected")
        (self.root / "Objs").symlink_to(protected.parent, target_is_directory=True)
        self.assertNotEqual(self.run_ps().returncode, 0)
        self.assertEqual(self.calls(), [])
        self.assertEqual(protected.read_bytes(), b"protected")

    def test_missing_exact_output_rejects_decoy(self):
        self.ok(self.run_ps("clean"))
        self.put("test/aaa-stale/Potrace.4DX", b"decoy")
        self.assertNotEqual(self.run_ps("normalize").returncode, 0)
        self.assertFalse((self.root / native.WINDOWS).exists())

    def test_malformed_pe_rejected(self):
        self.ok(self.run_ps("clean"))
        source = "test/plugins/Potrace.bundle/contents/windows64/Potrace.4DX"
        for data in (b"", b"MZ", b"x" * 256, b"MZ" + b"\xff" * 254):
            with self.subTest(data=data[:4]):
                self.put(source, data)
                self.assertNotEqual(self.run_ps("normalize").returncode, 0)


class ConfigurationTests(unittest.TestCase):
    def test_exact_windows_path_matches_project_and_artifact(self):
        namespace = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
        project = ET.parse(ROOT / "Potrace.vcxproj")
        condition = "'$(Configuration)|$(Platform)'=='Release|x64'"
        outdirs = [node.text for node in project.findall(".//m:OutDir", namespace)
                   if node.get("Condition") == condition]
        extensions = [node.text for node in project.findall(".//m:TargetExt", namespace)
                      if node.get("Condition") == condition]
        self.assertEqual(outdirs, [
            "$(ProjectDir)test\\plugins\\$(ProjectName).bundle\\contents\\windows64\\"])
        self.assertEqual(extensions, [".4DX"])
        helper = (SCRIPTS / "native-windows.ps1").read_text()
        self.assertIn("test/plugins/Potrace.bundle/contents/windows64/Potrace.4DX", helper)
        self.assertIn(str(native.WINDOWS), helper)
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        self.assertIn(
            "path: ${{ env.BASE_DIR }}/Plugins/${{ env.PRODUCT_NAME }}.bundle/"
            "Contents/Windows64/${{ env.PRODUCT_NAME }}.4DX", workflow)
        self.assertIn("path: winbuild", workflow)

    def test_committed_resources_are_not_all_xcode_copy_resources(self):
        project = (ROOT / "Potrace.xcodeproj/project.pbxproj").read_text()
        phase = project.split("/* Begin PBXResourcesBuildPhase section */")[1].split(
            "/* End PBXResourcesBuildPhase section */")[0]
        self.assertIn("InfoPlist.strings in Resources", phase)
        self.assertNotIn("constants.xlf", phase)
        self.assertNotIn("manifest.json", phase)
        for relative in ("Resources/constants.xlf", "Resources/manifest.json",
                         "QuickLook/Thumbnail.png"):
            self.assertTrue((ROOT / native.CONTENTS / relative).is_file())


if __name__ == "__main__":
    unittest.main()
