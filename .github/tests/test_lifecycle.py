import contextlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / ".github/scripts"))
import release
import source_version as source


@contextlib.contextmanager
def cwd(path):
    previous = Path.cwd()
    os.chdir(path)
    try:
        yield
    finally:
        os.chdir(previous)


def git(path, *args):
    location = ["--git-dir", str(path)] if path.name.endswith(".git") else ["-C", str(path)]
    return subprocess.run(
        ["git", *location, *args], check=True, text=True,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    ).stdout.strip()


def copy_source(destination):
    for path in source.SOURCE_PATHS:
        (destination / path).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / path, destination / path)


def entry(tag="v9.8.7", draft=False):
    return {"id": 42, "tag_name": tag, "draft": draft, "prerelease": False}


class SourceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix=".lifecycle-source-tests-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        copy_source(self.root)
        self.current = source.read(self.root)
        self.drift = ".".join(str(int(part) + 10) for part in self.current.split("."))

    def plist(self, **values):
        path = self.root / source.PLIST
        data = plistlib.loads(path.read_bytes())
        data.update(values)
        path.write_bytes(plistlib.dumps(data))

    def test_live_metadata_and_all_bump_modes(self):
        parts = list(map(int, self.current.split(".")))
        for mode, expected in (
            ("patch", [parts[0], parts[1], parts[2] + 1]),
            ("minor", [parts[0], parts[1] + 1, 0]),
            ("major", [parts[0] + 1, 0, 0]),
        ):
            with self.subTest(mode=mode):
                copy_source(self.root)
                expected = ".".join(map(str, expected))
                self.assertEqual(source.bump(mode, self.root), expected)
                self.assertEqual(source.read(self.root), expected)
                self.assertNotIn("CFBundleVersion",
                                 plistlib.loads((self.root / source.PLIST).read_bytes()))
                for key in source.KEYS:
                    self.assertEqual((self.root / source.PROJECT).read_text().count(
                        f"{key} = {expected};"), source.SETTING_COUNT)

    def test_invalid_missing_and_drifted_project_settings(self):
        path = self.root / source.PROJECT
        original = path.read_text()
        for key in source.KEYS:
            for bad in ("1.2", "v1.2.3", "01.2.3", "1.2.3-beta", "$(UNKNOWN)", "", self.drift):
                with self.subTest(key=key, bad=bad):
                    path.write_text(original.replace(f"{key} = {self.current};",
                                                     f"{key} = {bad};", 1))
                    with self.assertRaises(ValueError):
                        source.bump("patch", self.root)
            path.write_text(original.replace(f"{key} = {self.current};", "", 1))
            with self.assertRaises(ValueError):
                source.read(self.root)
        for bad in ("v2.0.0", "2.0.0-beta", "$(UNKNOWN)", "", "$(CURRENT_PROJECT_VERSION)"):
            path.write_text(source.SETTING.sub(
                lambda m: f"{m[1]}{bad}{m[4]}" if m[2] == "CURRENT_PROJECT_VERSION" else m[0],
                original,
            ))
            with self.subTest(build=bad), self.assertRaises(ValueError):
                source.read(self.root)
        path.write_text(original.replace(f"CURRENT_PROJECT_VERSION = {self.current};", "", 1))
        with self.assertRaises(ValueError):
            source.read(self.root)

    def test_plist_invalid_drift_and_missing(self):
        for key in ("CFBundleShortVersionString", "CFBundleVersion"):
            for value in ("$(UNKNOWN)", "$(PRODUCT_NAME)", "", "invalid", 123,
                          "1.2", "14.3", "7", self.drift):
                with self.subTest(key=key, value=value):
                    copy_source(self.root)
                    self.plist(**{key: value})
                    with self.assertRaises(ValueError):
                        source.read(self.root)
        copy_source(self.root)
        path = self.root / source.PLIST
        data = plistlib.loads(path.read_bytes())
        del data["CFBundleShortVersionString"]
        path.write_bytes(plistlib.dumps(data))
        with self.assertRaises(ValueError):
            source.read(self.root)

    def test_valid_refs_preserved_and_optional_bundle_version_co_bumped(self):
        for reference in source.REFERENCES:
            with self.subTest(reference=reference):
                copy_source(self.root)
                self.plist(CFBundleShortVersionString=reference, CFBundleVersion=reference)
                path = self.root / source.PROJECT
                path.write_text(source.SETTING.sub(
                    lambda m: f'{m[1]}"$(MARKETING_VERSION)"{m[4]}'
                    if m[2] == "CURRENT_PROJECT_VERSION" else m[0], path.read_text()
                ))
                new = source.bump("patch", self.root)
                data = plistlib.loads((self.root / source.PLIST).read_bytes())
                self.assertEqual(data["CFBundleShortVersionString"], reference)
                self.assertEqual(data["CFBundleVersion"], reference)
                self.assertEqual(path.read_text().count('"$(MARKETING_VERSION)"'), source.SETTING_COUNT)
                self.assertEqual(source.read(self.root), new)
        copy_source(self.root)
        self.plist(CFBundleVersion=self.current)
        new = source.bump("minor", self.root)
        self.assertEqual(plistlib.loads((self.root / source.PLIST).read_bytes())["CFBundleVersion"], new)

    def test_cyclic_current_project_ref_rejected(self):
        path = self.root / source.PROJECT
        path.write_text(source.SETTING.sub(
            lambda m: f'{m[1]}"$(CURRENT_PROJECT_VERSION)"{m[4]}'
            if m[2] == "CURRENT_PROJECT_VERSION" else m[0], path.read_text()
        ))
        with self.assertRaises(ValueError):
            source.read(self.root)

    def test_stable_fixture_coordinated_contract(self):
        self.assertEqual(source.SETTING_COUNT, 6)
        project = self.root / source.PROJECT
        project.write_text(
            ('MARKETING_VERSION = 3.2.1;\nCURRENT_PROJECT_VERSION = 3.2.1;\n') * source.SETTING_COUNT
        )
        self.plist(CFBundleShortVersionString="3.2.1", CFBundleVersion="3.2.1")
        localized = self.root / source.LOCALIZED
        localized.write_text(
            'CFBundleName = "Potrace";\nCFBundleShortVersionString = "3.2.1";\n'
            'CFBundleGetInfoString = "Potrace version 3.2.1, Copyright 2019 miyako.";\n',
            encoding="utf-8-sig",
        )
        self.assertEqual(source.read(self.root), "3.2.1")
        self.assertEqual(source.bump("minor", self.root), "3.3.0")
        self.assertEqual(project.read_text().count("CURRENT_PROJECT_VERSION = 3.3.0;"), source.SETTING_COUNT)
        self.assertEqual(plistlib.loads((self.root / source.PLIST).read_bytes())["CFBundleVersion"], "3.3.0")
        self.plist(CFBundleShortVersionString="$(CURRENT_PROJECT_VERSION)")
        self.assertEqual(source.read(self.root), "3.3.0")
        project.write_text(project.read_text().replace("CURRENT_PROJECT_VERSION = 3.3.0;",
                                                      "CURRENT_PROJECT_VERSION = 14.3;"))
        with self.assertRaises(ValueError):
            source.read(self.root)

    def test_current_drift_rejected(self):
        path = self.root / source.PROJECT
        original = path.read_text()
        for old in (self.drift, "14.3", "77"):
            for count in (1, source.SETTING_COUNT):
                with self.subTest(old=old, count=count):
                    path.write_text(original.replace(
                        f"CURRENT_PROJECT_VERSION = {self.current};",
                        f"CURRENT_PROJECT_VERSION = {old};", count))
                    with self.assertRaises(ValueError):
                        source.read(self.root)

    def test_prebuilt_schema_and_localization_counters_ignored(self):
        excluded = (
            "test/Plugins/Potrace.bundle/Contents/Info.plist",
            "test/Data/data.4DD", "test/Project/Sources/catalog.4DCatalog", "manifest.json",
            "constants.xlf", "a/libpotrace.a", "lib/windows/potrace.lib", "lib/windows64/potrace.lib",
            "4D Plugin API/4DPluginAPI.h", "4DPlugin-Potrace.h",
            "Potrace.vcxproj", "Potrace.vcxproj.filters", "Potrace.sln",
        )
        for name in excluded:
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((ROOT / name).read_bytes())
        before = {name: (self.root / name).read_bytes() for name in excluded}
        source.bump("major", self.root)
        self.assertEqual(before, {name: (self.root / name).read_bytes() for name in excluded})

    def test_localized_version_validation_and_bumping(self):
        path = self.root / source.LOCALIZED
        original = path.read_text(encoding="utf-8-sig")
        for malformed in (
            original.replace(f'CFBundleShortVersionString = "{self.current}"',
                             'CFBundleShortVersionString = "1.0"'),
            original.replace(f"Potrace version {self.current},", "Potrace version invalid,"),
            original + f'\nCFBundleShortVersionString = "{self.current}";\n',
            original.replace("CFBundleShortVersionString", "UnknownKey"),
        ):
            with self.subTest(malformed=malformed):
                path.write_text(malformed, encoding="utf-8-sig")
                with self.assertRaises(ValueError):
                    source.read(self.root)
        path.write_text(original, encoding="utf-8-sig")
        new = source.bump("minor", self.root)
        self.assertIn(f'CFBundleShortVersionString = "{new}";', path.read_text(encoding="utf-8-sig"))
        self.assertIn(f"Potrace version {new}, Copyright 2019 miyako.",
                      path.read_text(encoding="utf-8-sig"))
        self.assertTrue(path.read_bytes().startswith(b"\xef\xbb\xbf"))

    def test_all_metadata_byte_preserving_endings_bom_and_modes(self):
        for newline in ("\n", "\r", "\r\n", "mixed"):
            for bom in (b"", b"\xef\xbb\xbf"):
                with self.subTest(newline=newline, bom=bom):
                    copy_source(self.root)
                    originals = {}
                    for path in source.SOURCE_PATHS:
                        target = self.root / path
                        lines = source.text(target).splitlines(keepends=True)
                        value = "".join(
                            line.rstrip("\r\n") + (("\r", "\r\n", "\n")[i % 3]
                                                   if newline == "mixed" else newline)
                            if line.endswith(("\r", "\n")) else line
                            for i, line in enumerate(lines)
                        )
                        target.write_bytes(bom + value.encode())
                        target.chmod(0o755)
                        originals[path] = target.read_bytes()
                    new = source.bump("minor", self.root)
                    for path in source.SOURCE_PATHS:
                        expected = originals[path].replace(self.current.encode(), new.encode())
                        self.assertEqual((self.root / path).read_bytes(), expected)
                        self.assertEqual((self.root / path).stat().st_mode & 0o777, 0o755)

    def test_quoted_coordinated_settings_and_wrong_plist_root(self):
        path = self.root / source.PROJECT
        value = path.read_text()
        for key in source.KEYS:
            value = value.replace(f"{key} = {self.current};", f'{key} = "{self.current}";')
        path.write_text(value)
        new = source.bump("patch", self.root)
        for key in source.KEYS:
            self.assertEqual(path.read_text().count(f'{key} = "{new}";'), source.SETTING_COUNT)
        (self.root / source.PLIST).write_bytes(plistlib.dumps([]))
        with self.assertRaisesRegex(ValueError, "dictionary"):
            source.read(self.root)

    def test_setting_whitespace_preserved_and_extra_settings_rejected(self):
        path = self.root / source.PROJECT
        original = path.read_bytes()
        for quoted in (False, True):
            copy_source(self.root)
            old = self.current.encode()
            value = b'"' + old + b'"' if quoted else old
            before = original
            for key in source.KEYS:
                before = before.replace(key.encode() + b" = " + old + b";",
                                        key.encode() + b" = \t" + value + b" \t;")
            path.write_bytes(before)
            new = source.bump("patch", self.root)
            self.assertEqual(path.read_bytes(), before.replace(old, new.encode()))
        path.write_bytes(original + b"\nMARKETING_VERSION = " + self.current.encode() + b";\n")
        with self.assertRaises(ValueError):
            source.read(self.root)


class AvailabilityTests(unittest.TestCase):
    def test_page_two_draft_published_and_successful_absence(self):
        for draft in (True, False):
            with self.subTest(draft=draft), patch.object(release, "remote_ref", return_value=None), \
                    patch.object(release, "api", side_effect=[
                        [entry()] * 100, [entry("v1.2.3", draft)]
                    ]) as api:
                with self.assertRaisesRegex(ValueError, "draft or published"):
                    release.available("owner/repo", "v1.2.3")
                self.assertEqual(api.call_args_list[1].args[0],
                                 "repos/owner/repo/releases?per_page=100&page=2")
        with patch.object(release, "remote_ref", return_value=None), \
                patch.object(release, "api", side_effect=[[entry()] * 100, []]) as api:
            release.available("owner/repo", "v1.2.3")
            self.assertEqual(api.call_count, 2)

    def test_later_full_pages_are_not_truncated(self):
        for final in ([], [entry("v1.2.3", True)], [entry("v1.2.3", False)]):
            with self.subTest(final=final), \
                    patch.object(release, "remote_ref", return_value=None), \
                    patch.object(release, "api", side_effect=[
                        [entry()] * 100, [entry()] * 100, final]) as api:
                if final:
                    with self.assertRaisesRegex(ValueError, "draft or published"):
                        release.available("owner/repo", "v1.2.3")
                else:
                    release.available("owner/repo", "v1.2.3")
                self.assertEqual(api.call_args_list[2].args[0],
                                 "repos/owner/repo/releases?per_page=100&page=3")

    def test_errors_and_malformed_every_page_fail_closed(self):
        errors = [
            *(subprocess.CalledProcessError(1, ["gh", "api", str(status)])
              for status in (401, 403, 404, 500)),
            OSError("network"), json.JSONDecodeError("bad", "?", 0),
        ]
        bad_pages = [{}, None, "[]", [None], [{}], [dict(entry(), id=True)],
                     [dict(entry(), id=0)], [dict(entry(), tag_name=None)],
                     [dict(entry(), tag_name="")], [dict(entry(), draft="false")],
                     [dict(entry(), prerelease=None)], [entry()] * 101]
        for page in (1, 2, 3):
            for response in errors + bad_pages:
                with self.subTest(page=page, response=response), \
                        patch.object(release, "remote_ref", return_value=None), \
                        patch.object(release, "api", side_effect=(
                            [[entry()] * 100] * (page - 1) + [response])):
                    with self.assertRaises((ValueError, OSError, subprocess.CalledProcessError)):
                        release.available("owner/repo", "v1.2.3")

    def test_remote_errors_and_malformed_fail_closed(self):
        with patch.object(release, "remote_ref", return_value="a" * 40), \
                patch.object(release, "api") as api:
            with self.assertRaises(ValueError):
                release.available("o/r", "v1.2.3")
            api.assert_not_called()
        for output in ("invalid\trefs/tags/v1.2.3", "a" * 40,
                       "a" * 40 + "\trefs/tags/wrong",
                       ("a" * 40 + "\trefs/tags/v1.2.3\n") * 2):
            with self.subTest(output=output), patch.object(release, "run", return_value=output):
                with self.assertRaises(ValueError):
                    release.remote_ref("refs/tags/v1.2.3")
        with patch.object(release, "run", side_effect=OSError("network")):
            with self.assertRaises(OSError):
                release.available("o/r", "v1.2.3")

    def test_transport_explicit_methods_and_no_retry(self):
        with patch.object(release, "run", return_value="[]") as run:
            release.api("repos/o/r/releases?per_page=100&page=2")
            self.assertEqual(run.call_args.args, ("gh", "api", "--method", "GET",
                             "repos/o/r/releases?per_page=100&page=2"))
        with patch.object(release, "run", return_value='{"id": 7}') as run:
            payload = {"tag_name": "v1.2.3", "draft": False}
            release.api("repos/o/r/releases", payload)
            self.assertIn("POST", run.call_args.args)
            self.assertEqual(json.loads(run.call_args.kwargs["input"]), payload)
        with patch.object(release, "run", side_effect=subprocess.CalledProcessError(1, "gh")) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                release.api("repos/o/r/releases")
            self.assertEqual(run.call_count, 1)
        with patch.object(release, "run", return_value='{"id": 8}') as run:
            asset = Path("Potrace-1.2.3.zip")
            release.api("https://uploads.github.com/repos/o/r/releases/123/assets", asset=asset)
            self.assertEqual(run.call_args.args, (
                "gh", "api", "--method", "POST",
                "https://uploads.github.com/repos/o/r/releases/123/assets",
                "--header", "Content-Type: application/octet-stream", "--input", str(asset)))


class GitTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix=".lifecycle-git-tests-", dir=ROOT)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.remote = self.root / "remote.git"
        self.local = self.root / "local"
        self.peer = self.root / "peer"
        git(self.root, "init", "--bare", str(self.remote))
        git(self.root, "init", "-b", "selected", str(self.local))
        for key, value in (("user.name", "Lifecycle Test"), ("user.email", "test@example.invalid"),
                           ("push.followTags", "true")):
            git(self.local, "config", key, value)
        git(self.local, "commit", "--allow-empty", "-m", "ancestor")
        self.ancestor = git(self.local, "rev-parse", "HEAD")
        copy_source(self.local)
        shutil.copytree(ROOT / ".github/scripts", self.local / ".github/scripts",
                        ignore=shutil.ignore_patterns("__pycache__"))
        git(self.local, "add", ".")
        git(self.local, "commit", "-m", "fixture")
        git(self.local, "remote", "add", "origin", str(self.remote))
        git(self.local, "push", "origin", "HEAD:refs/heads/selected")
        self.sha = git(self.local, "rev-parse", "HEAD")
        self.version = source.read(self.local)
        self.tag = f"v{self.version}"
        self.published = None
        self.uploaded = []
        git(self.root, "clone", "--branch", "selected", str(self.remote), str(self.peer))
        git(self.peer, "config", "user.name", "Peer")
        git(self.peer, "config", "user.email", "peer@example.invalid")
        self.enter = cwd(self.local)
        self.enter.__enter__()
        self.addCleanup(self.enter.__exit__, None, None, None)

    def advance_peer(self):
        (self.peer / "peer.txt").write_text("concurrent branch update")
        git(self.peer, "add", ".")
        git(self.peer, "commit", "-m", "peer")
        git(self.peer, "push", "origin", "HEAD:refs/heads/selected")

    def test_bump_only_explicit_branch_source_no_follow_tags(self):
        git(self.local, "tag", "-a", "unrelated-annotated", "-m", "must not follow")
        git(self.remote, "update-ref", "refs/heads/other", self.sha)
        real_run = release.run
        with patch.object(release, "run", wraps=real_run) as run:
            release.bump_branch(self.sha, "selected", "patch")
        pushes = [call.args for call in run.call_args_list if "push" in call.args]
        self.assertEqual(len(pushes), 1)
        self.assertIn("--no-follow-tags", pushes[0])
        self.assertFalse(any(arg.startswith(("--force", "+")) for arg in pushes[0]))
        self.assertNotEqual(git(self.remote, "rev-parse", "refs/heads/selected"), self.sha)
        self.assertEqual(git(self.remote, "rev-parse", "refs/heads/other"), self.sha)
        self.assertEqual(git(self.remote, "tag", "--list"), "")
        self.assertEqual(git(self.local, "rev-parse", "HEAD^"), self.sha)
        self.assertEqual(git(self.local, "status", "--porcelain"), "")
        self.assertEqual(set(git(self.local, "diff-tree", "--no-commit-id", "--name-only",
                                 "-r", "HEAD").splitlines()), set(map(str, source.SOURCE_PATHS)))
        self.assertIn("Co-authored-by: Copilot App <223556219+Copilot@users.noreply.github.com>",
                      git(self.local, "log", "-1", "--format=%B"))

    def test_cli_bytecode_keeps_clean_checkout(self):
        env = os.environ.copy()
        env.pop("PYTHONDONTWRITEBYTECODE", None)
        subprocess.run(
            [sys.executable, ".github/scripts/release.py", "bump", "--sha", self.sha,
             "--branch", "selected", "--mode", "patch"],
            env=env, check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(git(self.local, "status", "--porcelain"), "")
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_preparation_races(self):
        self.advance_peer()
        with self.assertRaisesRegex(ValueError, "moved"):
            release.bump_branch(self.sha, "selected", "patch")
        self.assertEqual(git(self.local, "rev-parse", "HEAD"), self.sha)

    def test_race_during_preparation(self):
        real_bump = release.bump

        def concurrent_bump(mode):
            result = real_bump(mode)
            self.advance_peer()
            return result

        with patch.object(release, "bump", side_effect=concurrent_bump):
            with self.assertRaisesRegex(ValueError, "moved during"):
                release.bump_branch(self.sha, "selected", "minor")
        self.assertEqual(release.remote_ref("refs/heads/selected"),
                         git(self.peer, "rev-parse", "HEAD"))

    def advertised_race(self, kind):
        real_run = release.run
        real_remote_ref = release.remote_ref
        expected = None
        pushes = 0
        reads = 0

        def read(ref):
            nonlocal reads
            result = real_remote_ref(ref)
            reads += 1
            return result

        def concurrent_push(*args, **kwargs):
            nonlocal expected, pushes
            # Race strictly after the final read, before receive-pack advertisement.
            if "push" in args:
                self.assertEqual(reads, 2)
                pushes += 1
                if kind == "rewind":
                    expected = self.ancestor
                    git(self.remote, "update-ref", "refs/heads/selected", expected)
                elif kind == "delete":
                    git(self.remote, "update-ref", "-d", "refs/heads/selected")
                elif kind == "advance":
                    self.advance_peer()
                    expected = git(self.peer, "rev-parse", "HEAD")
                elif kind == "same-result":
                    expected = git(self.local, "rev-parse", "HEAD")
                    git(self.remote, "fetch", "--no-tags", str(self.local), "HEAD")
                    git(self.remote, "update-ref", "refs/heads/selected", expected)
                else:
                    self.fail(kind)
            return real_run(*args, **kwargs)

        with patch.object(release, "run", side_effect=concurrent_push), \
                patch.object(release, "remote_ref", side_effect=read):
            with self.assertRaises(subprocess.CalledProcessError):
                release.bump_branch(self.sha, "selected", "patch")
        self.assertEqual(pushes, 1)
        self.assertEqual(real_remote_ref("refs/heads/selected"), expected)
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_advertised_rewind(self):
        self.advertised_race("rewind")

    def test_advertised_deletion(self):
        self.advertised_race("delete")

    def test_advertised_advance(self):
        self.advertised_race("advance")

    def test_advertised_same_result(self):
        self.advertised_race("same-result")

    def test_missing_nonexecutable_guard_and_dirty_source(self):
        guard = self.local / ".github/scripts/bump-hooks/pre-push"
        guard.chmod(0o644)
        for absent in (False, True):
            if absent:
                guard.unlink()
            with self.subTest(absent=absent), \
                    patch.object(release, "__file__", str(self.local / ".github/scripts/release.py")):
                with self.assertRaisesRegex(ValueError, "guard is missing or not executable"):
                    release.bump_branch(self.sha, "selected", "patch")
            self.assertEqual(git(self.local, "rev-parse", "HEAD"), self.sha)
        with self.assertRaisesRegex(ValueError, "clean checkout"):
            release.bump_branch(self.sha, "selected", "patch")

    def guard_loss_after_final_read(self, missing):
        runtime = self.root / "runtime"
        shutil.copytree(self.local / ".github/scripts", runtime)
        guard = runtime / "bump-hooks/pre-push"
        real_remote_ref = release.remote_ref
        reads = 0

        def lose_guard(ref):
            nonlocal reads
            result = real_remote_ref(ref)
            reads += 1
            if reads == 2:
                if missing:
                    guard.unlink()
                else:
                    guard.chmod(0o644)
            return result

        with patch.object(release, "__file__", str(runtime / "release.py")), \
                patch.object(release, "remote_ref", side_effect=lose_guard), \
                patch.object(release.os, "access", wraps=os.access) as access:
            with self.assertRaisesRegex(ValueError, "guard is missing or not executable"):
                release.bump_branch(self.sha, "selected", "patch")
            self.assertEqual(access.call_count, 1 if missing else 2)
        self.assertEqual(reads, 2)
        self.assertEqual(real_remote_ref("refs/heads/selected"), self.sha)
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_nonexecutable_guard_rechecked_after_final_remote_read(self):
        self.guard_loss_after_final_read(False)

    def test_missing_guard_rechecked_after_final_remote_read(self):
        self.guard_loss_after_final_read(True)

    def test_immutable_invalid_sha_and_branch(self):
        for sha in ("selected", "a" * 40, "a" * 39, "a" * 41):
            with self.subTest(sha=sha), self.assertRaises(ValueError):
                release.selected_sha(sha)
        release.selected_sha(self.sha)
        with self.assertRaises(subprocess.CalledProcessError):
            release.bump_branch(self.sha, "invalid..branch", "patch")

    def test_preflight_clean_and_final_source_drift(self):
        (self.local / source.PLIST).write_text("dirty source")
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api") as api:
            with self.assertRaisesRegex(ValueError, "clean source"):
                release.preflight(self.sha)
            api.assert_not_called()
        copy_source(self.local)
        paths = self.assets()
        source.bump("patch")
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api") as api:
            with self.assertRaisesRegex(ValueError, "differs from the selected commit"):
                release.publish(self.sha, paths)
            api.assert_not_called()
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_preflight_immutable_clean_source_and_remote_absence(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", return_value=[]) as api:
            self.assertEqual(release.preflight(self.sha), (self.version, self.tag))
            api.assert_called_once_with("repos/owner/repo/releases?per_page=100&page=1")
        git(self.local, "commit", "--allow-empty", "-m", "different selected checkout")
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api") as api:
            with self.assertRaisesRegex(ValueError, "Checkout does not match"):
                release.preflight(self.sha)
            api.assert_not_called()
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_bump_rejects_merge_commit_before_push(self):
        real_run = release.run

        def merge_after_commit(*args, **kwargs):
            result = real_run(*args, **kwargs)
            if args[:2] == ("git", "commit"):
                tree = git(self.local, "rev-parse", "HEAD^{tree}")
                new = subprocess.run(
                    ["git", "commit-tree", tree, "-p", self.sha, "-p", self.ancestor],
                    input="merge bump fixture\n", text=True, check=True,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                ).stdout.strip()
                git(self.local, "update-ref", "HEAD", new)
            return result

        with patch.object(release, "run", side_effect=merge_after_commit) as run:
            with self.assertRaisesRegex(ValueError, "single-parent direct child"):
                release.bump_branch(self.sha, "selected", "patch")
            self.assertFalse(any("push" in call.args for call in run.call_args_list))
        self.assertEqual(release.remote_ref("refs/heads/selected"), self.sha)
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_guard_local_sha_target_parent_and_missing_env(self):
        guard = ROOT / ".github/scripts/bump-hooks/pre-push"
        git(self.local, "commit", "--allow-empty", "-m", "direct child")
        new = git(self.local, "rev-parse", "HEAD")
        env = {**os.environ, "BUMP_EXPECTED_SHA": self.sha,
               "BUMP_EXPECTED_REF": "refs/heads/selected", "BUMP_NEW_SHA": new}
        valid = f"{new} {new} refs/heads/selected {self.sha}\n"
        result = subprocess.run([str(guard)], input=valid, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)
        for row in (
            f"{new} {new} refs/heads/wrong {self.sha}\n",
            f"HEAD {new} refs/heads/selected {self.sha}\n",
            f"{new} {self.ancestor} refs/heads/selected {self.sha}\n",
            f"{new} {new} refs/heads/selected {self.ancestor}\n",
            valid * 2, "",
        ):
            result = subprocess.run([str(guard)], input=row, env=env, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
        result = subprocess.run([str(guard)], input=valid, env={}, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(result.returncode, 0)

    def test_bump_rejects_non_source_changes_in_commit_hook(self):
        real_run = release.run

        def extra_change(*args, **kwargs):
            if args[:2] == ("git", "commit"):
                (self.local / "unrelated.txt").write_text("not version metadata")
                git(self.local, "add", "unrelated.txt")
            return real_run(*args, **kwargs)

        with patch.object(release, "run", side_effect=extra_change) as run:
            with self.assertRaisesRegex(ValueError, "source version metadata only"):
                release.bump_branch(self.sha, "selected", "patch")
            self.assertFalse(any("push" in call.args for call in run.call_args_list))
        self.assertEqual(release.remote_ref("refs/heads/selected"), self.sha)

    def assets(self):
        paths = [Path(f"Potrace-{self.version}.{ext}") for ext in ("zip", "dmg")]
        for asset in paths:
            asset.write_bytes(b"fixture asset")
        return paths

    def test_guard_rejects_multiple_parents_even_with_selected_first_parent(self):
        tree = git(self.local, "rev-parse", "HEAD^{tree}")
        new = subprocess.run(
            ["git", "commit-tree", tree, "-p", self.sha, "-p", self.ancestor],
            input="merge fixture\n", text=True, check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        ).stdout.strip()
        git(self.local, "update-ref", "HEAD", new)
        guard = ROOT / ".github/scripts/bump-hooks/pre-push"
        result = subprocess.run(
            [str(guard)], input=f"{new} {new} refs/heads/selected {self.sha}\n",
            env={**os.environ, "BUMP_EXPECTED_SHA": self.sha,
                 "BUMP_EXPECTED_REF": "refs/heads/selected", "BUMP_NEW_SHA": new},
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("advertised branch state differs", result.stderr)
        result = subprocess.run(
            ["git", "-c", f"core.hooksPath={guard.parent}", "-c", "push.followTags=false",
             "push", "--porcelain", "--no-follow-tags", "origin",
             f"{new}:refs/heads/selected"],
            env={**os.environ, "BUMP_EXPECTED_SHA": self.sha,
                 "BUMP_EXPECTED_REF": "refs/heads/selected", "BUMP_NEW_SHA": new},
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("advertised branch state differs", result.stderr)
        self.assertEqual(release.remote_ref("refs/heads/selected"), self.sha)

    def publication_api(self, calls, fail=None, race=None):
        def api(endpoint, payload=None, asset=None):
            calls.append((endpoint, payload, asset))
            if "/releases?" in endpoint:
                return []
            if endpoint.endswith("/git/refs"):
                if race:
                    git(self.remote, "update-ref", f"refs/tags/{self.tag}", race)
                # Atomic create-only bare Git models POST refs, including same-SHA rejection.
                git(self.remote, "update-ref", payload["ref"], payload["sha"], "0" * 40)
                return {"ref": payload["ref"], "object": {"sha": payload["sha"]}}
            if endpoint.endswith("/releases"):
                if fail == "release":
                    raise subprocess.CalledProcessError(1, ["gh", "api"])
                self.published = {"id": 123, "tag_name": self.tag, "draft": False, "prerelease": False}
                return self.published
            if asset is not None:
                if fail == asset.suffix:
                    raise subprocess.CalledProcessError(1, ["gh", "api"])
                self.uploaded.append(asset.name)
                return {"id": 456, "name": asset.name,
                        "size": asset.stat().st_size, "state": "uploaded"}
            raise AssertionError(endpoint)
        return api

    def test_create_only_sha_new_release_id_original_notes(self):
        calls = []
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls)):
            release.publish(self.sha, self.assets())
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)
        self.assertIn("/releases?", calls[0][0])
        self.assertTrue(calls[1][0].endswith("/git/refs"))
        self.assertEqual(calls[1][1], {"ref": f"refs/tags/{self.tag}", "sha": self.sha})
        self.assertEqual(calls[2][1], {
            "tag_name": self.tag, "target_commitish": self.sha, "name": self.tag,
            "draft": False, "prerelease": False, "body": ""})
        self.assertEqual(len(calls), 5)
        for endpoint, payload, asset in calls[3:]:
            self.assertIn("/releases/123/assets?name=", endpoint)
            self.assertIsNone(payload)
            self.assertIsNotNone(asset)

    def tag_race(self, sha):
        calls = []
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls, race=sha)):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 2)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), sha)
        self.assertIsNone(self.published)

    def test_same_sha_tag_race(self):
        self.tag_race(self.sha)

    def test_different_sha_tag_race(self):
        self.advance_peer()
        self.tag_race(git(self.peer, "rev-parse", "HEAD"))

    def test_release_post_failure_retains_tag(self):
        calls = []
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls, fail="release")):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 3)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)
        self.assertIsNone(self.published)
        self.assertEqual(self.uploaded, [])

    def test_upload_failure_retains_tag_release_earlier_asset(self):
        calls = []
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls, fail=".dmg")):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 5)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)
        self.assertEqual(self.published["id"], 123)
        self.assertEqual(self.uploaded, [f"Potrace-{self.version}.zip"])

    def test_first_upload_failure_retains_tag_and_release(self):
        calls = []
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls, fail=".zip")):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 4)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)
        self.assertEqual(self.published["id"], 123)
        self.assertEqual(self.uploaded, [])

    def test_tag_post_failure_does_not_create_release_or_upload(self):
        calls = []

        def failed_tag(endpoint, payload=None, asset=None):
            calls.append((endpoint, payload, asset))
            if "/releases?" in endpoint:
                return []
            raise subprocess.CalledProcessError(1, ["gh", "api", "git/refs"])

        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=failed_tag):
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 2)
        self.assertEqual(git(self.remote, "tag", "--list"), "")
        self.assertIsNone(self.published)
        self.assertEqual(self.uploaded, [])

    def test_final_recheck_new_draft(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", return_value=[]):
            release.preflight(self.sha)
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", return_value=[entry(self.tag, True)]) as api:
            with self.assertRaisesRegex(ValueError, "draft or published"):
                release.publish(self.sha, self.assets())
            self.assertEqual(api.call_count, 1)
        self.assertEqual(git(self.remote, "tag", "--list"), "")

    def test_final_later_page_failure_stops_before_creation(self):
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", return_value=[]):
            release.preflight(self.sha)
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=[
                    [entry()] * 100, subprocess.CalledProcessError(1, ["gh", "api", "404"])
                ]) as api:
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.sha, self.assets())
            self.assertEqual(api.call_count, 2)
        self.assertEqual(git(self.remote, "tag", "--list"), "")
        self.assertIsNone(self.published)

    def test_remote_verification_blocks_release(self):
        calls = []
        real_remote_ref = release.remote_ref

        def incorrect_verification(ref):
            return "a" * 40 if real_remote_ref(ref) else None

        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=self.publication_api(calls)), \
                patch.object(release, "remote_ref", side_effect=incorrect_verification):
            with self.assertRaisesRegex(ValueError, "exact-SHA verification"):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 2)
        self.assertIsNone(self.published)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)

    def test_malformed_post_and_upload_retain_state(self):
        for kind in ("release", "upload"):
            with self.subTest(kind=kind):
                if kind == "upload":
                    git(self.remote, "update-ref", "-d", f"refs/tags/{self.tag}")
                calls = []
                real_api = self.publication_api(calls)

                def malformed(endpoint, payload=None, asset=None):
                    result = real_api(endpoint, payload, asset)
                    if (kind == "release" and endpoint.endswith("/releases")) or (
                            kind == "upload" and asset is not None):
                        return {"id": None}
                    return result

                with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                        patch.object(release, "api", side_effect=malformed):
                    with self.assertRaisesRegex(ValueError, "Malformed"):
                        release.publish(self.sha, self.assets())
                self.assertEqual(len(calls), 3 if kind == "release" else 4)
                self.assertEqual(self.published["id"], 123)
                self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)

    def test_malformed_tag_post_response_retains_tag_without_release(self):
        calls = []
        real_api = self.publication_api(calls)

        def malformed(endpoint, payload=None, asset=None):
            result = real_api(endpoint, payload, asset)
            return {"ref": None} if endpoint.endswith("/git/refs") else result

        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api", side_effect=malformed):
            with self.assertRaisesRegex(ValueError, "exact-SHA verification"):
                release.publish(self.sha, self.assets())
        self.assertEqual(len(calls), 2)
        self.assertEqual(git(self.remote, "rev-parse", f"refs/tags/{self.tag}"), self.sha)
        self.assertIsNone(self.published)

    def test_missing_empty_wrong_assets_before_mutation(self):
        for paths in (
            [Path(f"Potrace-{self.version}.{ext}") for ext in ("zip", "dmg")],
            [Path("wrong.zip"), Path("wrong.dmg")],
        ):
            with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                    patch.object(release, "api") as api:
                with self.assertRaises(ValueError):
                    release.publish(self.sha, paths)
                api.assert_not_called()
        paths = self.assets()
        paths[0].write_bytes(b"")
        with patch.dict(os.environ, GITHUB_REPOSITORY="owner/repo"), \
                patch.object(release, "api") as api:
            with self.assertRaises(ValueError):
                release.publish(self.sha, paths)
            api.assert_not_called()


class WiringTests(unittest.TestCase):
    def test_manual_zero_inputs_immutable_native_gate_wiring(self):
        workflow = (ROOT / ".github/workflows/release.yml").read_text()
        self.assertRegex(workflow, r"(?m)^name: Build$")
        self.assertEqual(workflow.split("on:\n", 1)[1].split("\npermissions:", 1)[0].strip(),
                         "workflow_dispatch:")
        self.assertEqual(workflow.count("uses: actions/checkout@v4"), 3)
        self.assertEqual(workflow.count("ref: ${{ github.sha }}"), 3)
        self.assertIn("group: source-release-publication\n  cancel-in-progress: false", workflow)
        self.assertIn("needs: [version, build-windows]", workflow)
        self.assertIn("needs: version", workflow)
        self.assertEqual(workflow.count("GH_TOKEN: ${{ github.token }}"), 2)
        self.assertNotIn("REF_TYPE", workflow)
        gates = [
            'preflight --sha "$SELECTED_SHA"',
            "./.github/scripts/native-windows.ps1 -Action build",
            "upload windows binaries", "download windows binaries",
            "import Developer ID certificate", "store notarytool credentials",
            "python3 .github/scripts/native.py mac-build",
            "python3 .github/scripts/native.py verify",
            "python3 .github/scripts/source_version.py --verify-bundle test/Plugins/Potrace.bundle",
            "python3 .github/scripts/native.py merge", "- name: codesign",
            'python3 .github/scripts/native.py package --version "$RELEASE_VERSION"',
            "- name: create GitHub Release",
        ]
        self.assertEqual([workflow.index(gate) for gate in gates],
                         sorted(workflow.index(gate) for gate in gates))
        for required in ("codesign --verify --deep --strict",
                         "RELEASE_VERSION: ${{ needs.version.outputs.version }}",
                         "APPLE_DEVELOPER_ID_CERTIFICATE", "APPLE_DEVELOPER_ID_CERTIFICATE_PASSWORD",
                         "KEYCHAIN_PASSWORD", "NOTARYTOOL_APPLE_ID", "NOTARYTOOL_TEAM_ID",
                         "NOTARYTOOL_PASSWORD", "if-no-files-found: error",
                         "windows-binaries", "runs-on: windows-latest", "runs-on: macos-latest"):
            self.assertIn(required, workflow)
        windows = (ROOT / ".github/scripts/native-windows.ps1").read_text()
        for required in ("msbuild 'Potrace.sln'", "/p:Configuration=Release", "/p:Platform=x64",
                         "/p:PlatformToolset=v143", "/p:WindowsTargetPlatformVersion=10.0"):
            self.assertIn(required, windows)
        native = (ROOT / ".github/scripts/native.py").read_text()
        for required in ('"xcodebuild", "-project", "Potrace.xcodeproj", "-target", "Potrace"',
                         '"-configuration", "Default"', '"ARCHS=arm64 x86_64"',
                         '"ONLY_ACTIVE_ARCH=NO"', '"MACOSX_DEPLOYMENT_TARGET=11.0"',
                         '"CODE_SIGNING_ALLOWED=NO"', "CONFIGURATION_BUILD_DIR=",
                         '"xcrun", "notarytool", "submit"', '"--wait", "--output-format", "json"',
                         'result.get("status") != "Accepted"',
                         '"xcrun", "stapler", "staple"', "source_version.verify_bundle(BUNDLE)"):
            self.assertIn(required, native)
        for prohibited in ("continue-on-error", "always()", "${{ inputs.", "softprops/",
                           "github.ref_name", "branches:", "refs/heads/master"):
            self.assertNotIn(prohibited, workflow)
        final = workflow.split("- name: create GitHub Release", 1)[1]
        self.assertEqual(final.count("python3 "), 1)
        self.assertIn('publish --sha "$SELECTED_SHA" "$ZIP_PATH" "$DMG_PATH"', final)

    def test_bump_no_tag_or_destructive_publication(self):
        workflow = (ROOT / ".github/workflows/bump-version.yml").read_text()
        self.assertIn("ref: ${{ github.sha }}", workflow)
        self.assertIn("SELECTED_BRANCH: ${{ github.ref_name }}", workflow)
        self.assertIn('test "$REF_TYPE" = branch', workflow)
        for mode in ("patch", "minor", "major"):
            self.assertIn(f"- {mode}", workflow)
        helper = (ROOT / ".github/scripts/release.py").read_text()
        for prohibited in ('"DELETE"', '"PATCH"', "--clobber", "--force",
                           '"gh", "release", "create"', '"git", "tag"'):
            self.assertNotIn(prohibited, helper)
        self.assertIn('"push.followTags=false"', helper)
        self.assertIn('"--no-follow-tags"', helper)
        self.assertNotIn("workflow_dispatch", helper)

    def test_cli_asset_options(self):
        assets = [Path(f"Potrace-1.2.3.{ext}") for ext in ("zip", "dmg")]
        with patch.object(sys, "argv", ["release.py", "publish", "--sha", "a" * 40,
                                       *map(str, assets)]), \
                patch.object(release, "publish") as publish:
            release.main()
        publish.assert_called_once_with("a" * 40, assets)


if __name__ == "__main__":
    unittest.main()
