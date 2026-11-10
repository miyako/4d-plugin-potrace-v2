"""Compile actual helpers and the unchanged complete SDK; mock only Call4D."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("POTRACE_SOURCE_ROOT", ROOT))


@unittest.skipUnless(sys.platform == "darwin", "requires macOS CoreFoundation")
class StringOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.binary = Path(cls.temp.name) / "string-ownership"
        result = subprocess.run([
            "xcrun", "clang++", "-std=c++11", "-g",
            "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
            "-Wno-deprecated-declarations", "-Wno-multichar",
            "-I", str(SOURCE / "4D Plugin API"), "-I", str(SOURCE / "support"),
            "-x", "c++", str(SOURCE / "4D Plugin API/4DPluginAPI.c"),
            str(SOURCE / "support/4DPlugin-JSON.cpp"),
            str(ROOT / ".github/tests/string_ownership.cpp"),
            "-framework", "CoreFoundation", "-framework", "CoreGraphics",
            "-o", str(cls.binary),
        ], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)

    def test_ownership(self):
        for case in ("utf8", "wide", "text", "boolean", "number", "collection", "handoffs"):
            for empty in ("allocated", "null"):
                with self.subTest(case=case, empty=empty):
                    result = subprocess.run(
                        [str(self.binary), case, empty],
                        capture_output=True, text=True,
                        env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0",
                             "UBSAN_OPTIONS": "halt_on_error=1"},
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
