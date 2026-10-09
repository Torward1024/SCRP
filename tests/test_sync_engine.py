"""Offline dependency tests use an independent temporary local Git repository."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sync_engine", Path(__file__).resolve().parents[1]/"tools/sync_engine.py")
sync = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sync)


class SyncTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="scrp-sync-")
        self.root = Path(self.tmp.name)
        self.repo = self.root/"SCRP"
        self.repo.mkdir()
        sync.git("init", "-b", "main", cwd=self.repo)
        sync.git("config", "user.name", "SCRP fixture", cwd=self.repo)
        sync.git("config", "user.email", "fixture@example.invalid", cwd=self.repo)
        for name in ("CMakeLists.txt", "include/scrp/Json.h", "src/Json.cpp", "src/Vfs.cpp"):
            path = self.repo/name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n", encoding="utf-8")
        sync.git("add", ".", cwd=self.repo)
        sync.git("commit", "-m", "fixture", cwd=self.repo)
        self.revision = sync.git("rev-parse", "HEAD", cwd=self.repo).stdout.strip()
        self.project = self.root/"Game"
        self.project.mkdir()
        # Use Git's own path format: MSYS2 canonicalizes Windows drive paths.
        repository = sync.git("rev-parse", "--show-toplevel", cwd=self.repo).stdout.strip()
        self.lock = {"repository": repository, "revision": self.revision}

    def tearDown(self):
        self.assertEqual(self.root.resolve().parent, Path(tempfile.gettempdir()).resolve())
        self.tmp.cleanup()

    def test_clean_sibling_is_not_implicitly_selected(self):
        result = sync.resolve(self.project, self.lock)
        self.assertEqual(result, (self.project/"engine/SCRP").resolve())
        self.assertNotEqual(result, self.repo.resolve())
        self.assertEqual(sync.resolve(self.project, self.lock, offline=True), result)

    def test_dirty_sibling_is_preserved_and_cache_is_pinned(self):
        (self.repo/"src/Json.cpp").write_text("developer edits\n", encoding="utf-8")
        result = sync.resolve(self.project, self.lock)
        self.assertEqual(result, (self.project/"engine/SCRP").resolve())
        self.assertEqual((self.repo/"src/Json.cpp").read_text(), "developer edits\n")
        self.assertEqual(sync.git("branch", "--show-current", cwd=self.repo).stdout.strip(), "main")
        self.assertEqual(sync.git("rev-parse", "HEAD", cwd=result).stdout.strip(), self.revision)
        self.assertEqual(sync.resolve(self.project, self.lock, offline=True), result)

    def test_dirty_cache_is_never_overwritten(self):
        (self.repo/"dirty.txt").write_text("local\n")
        result = sync.resolve(self.project, self.lock)
        (result/"src/Json.cpp").write_text("cache edit\n")
        with self.assertRaisesRegex(RuntimeError, "Local changes"):
            sync.resolve(self.project, self.lock)

    def test_offline_absence_fails(self):
        (self.repo/"dirty.txt").write_text("local\n")
        with self.assertRaisesRegex(RuntimeError, "not available locally"):
            sync.resolve(self.project, self.lock, offline=True)

    def test_explicit_override_allows_development(self):
        (self.repo/"src/Json.cpp").write_text("developer edits\n")
        self.assertEqual(sync.resolve(self.project, self.lock, override=str(self.repo), offline=True), self.repo.resolve())

    def test_foreign_cache_is_never_changed(self):
        (self.repo/"dirty.txt").write_text("local\n")
        result = sync.resolve(self.project, self.lock)
        sync.git("remote", "set-url", "origin", "https://example.invalid/other.git", cwd=result)
        with self.assertRaisesRegex(RuntimeError, "Unexpected repository"):
            sync.resolve(self.project, self.lock)

    def test_newer_sibling_remains_on_its_branch(self):
        (self.repo/"src/Json.cpp").write_text("new engine\n")
        sync.git("add", ".", cwd=self.repo)
        sync.git("commit", "-m", "new version", cwd=self.repo)
        newer = sync.git("rev-parse", "HEAD", cwd=self.repo).stdout.strip()
        result = sync.resolve(self.project, self.lock)
        self.assertEqual(sync.git("rev-parse", "HEAD", cwd=result).stdout.strip(), self.revision)
        self.assertEqual(sync.git("rev-parse", "HEAD", cwd=self.repo).stdout.strip(), newer)
        self.assertEqual(sync.git("branch", "--show-current", cwd=self.repo).stdout.strip(), "main")


if __name__ == "__main__":
    unittest.main()
