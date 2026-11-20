#!/usr/bin/env python3
import importlib.util
import os
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/package-userdata-delta.py"
SPEC = importlib.util.spec_from_file_location("package_userdata_delta", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class DeltaPackageTests(unittest.TestCase):
    def test_only_changed_and_new_files_are_packaged(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            old, new = root / "old", root / "new"
            old.mkdir()
            new.mkdir()
            (old / "same").write_text("same")
            (new / "same").write_text("same")
            (old / "changed").write_text("old")
            (new / "changed").write_text("new")
            (old / "removed").write_text("keep old file on device")
            (new / "added").write_text("new file")
            output = root / "delta.zip"
            MODULE.main = MODULE.main
            import sys
            previous = sys.argv
            try:
                sys.argv = [str(SCRIPT), str(old), str(new), str(output)]
                MODULE.main()
            finally:
                sys.argv = previous
            with zipfile.ZipFile(output) as archive:
                names = set(archive.namelist())
                self.assertIn("payload/changed", names)
                self.assertIn("payload/added", names)
                self.assertNotIn("payload/same", names)
                self.assertNotIn("payload/removed", names)
                updater = archive.getinfo(MODULE.UPDATER_PATH)
                self.assertTrue((updater.external_attr >> 16) & stat.S_IXUSR)
                manifest = archive.read(MODULE.MANIFEST_PATH).decode()
                self.assertIn("\tchanged\n", manifest)
                self.assertIn("\tadded\n", manifest)

    def test_changed_symlink_is_rejected(self):
        if not hasattr(os, "symlink"):
            self.skipTest("symlinks unavailable")
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            old, new = root / "old", root / "new"
            old.mkdir()
            new.mkdir()
            os.symlink("one", old / "link")
            os.symlink("two", new / "link")
            import sys
            previous = sys.argv
            try:
                sys.argv = [str(SCRIPT), str(old), str(new), str(root / "delta.zip")]
                with self.assertRaises(SystemExit):
                    MODULE.main()
            finally:
                sys.argv = previous


if __name__ == "__main__":
    unittest.main()
