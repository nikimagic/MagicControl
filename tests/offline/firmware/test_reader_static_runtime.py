"""Offline path-safety tests for firmware-reader research runs; no hardware."""

from pathlib import Path
import importlib.util
import json
import tempfile
from types import SimpleNamespace
import unittest


REPO = Path(__file__).resolve().parents[3]
HELPER = REPO / "research/firmware-reader-static-support/runtime.py"
spec = importlib.util.spec_from_file_location("reader_static_runtime", HELPER)
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)


class ReaderStaticRuntimeTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="mc-reader-runtime-")
        self.base = Path(self.tmp.name).resolve()
        self.assertEqual(self.base.parent, Path(tempfile.gettempdir()).resolve())
        self.repo = self.base / "repo"
        self.repo.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_new_run_rejects_path_escape_without_creating_anything(self):
        invalid = ("", ".", "..", "../escape", "..\\escape", "/absolute",
                   "\\absolute", "C:\\outside", "C:relative", "a/b", "a\\b",
                   "a:b", "nul\0name")
        for label in invalid:
            with self.subTest(label=label), self.assertRaises(ValueError):
                runtime.new_run(self.repo, label)
        self.assertFalse((self.repo / "out").exists())
        self.assertEqual(sorted(p.name for p in self.base.iterdir()), ["repo"])

    def test_new_run_creates_unique_directories_under_out(self):
        first = runtime.new_run(self.repo, "fpt-inspection")
        marker = first / "result.txt"
        marker.write_text("preserve", encoding="utf-8")
        second = runtime.new_run(self.repo, "fpt-inspection")
        expected = (self.repo / "out/firmware-readers/fpt-inspection").resolve()
        self.assertNotEqual(first, second)
        self.assertEqual(first.parent.resolve(), expected)
        self.assertEqual(second.parent.resolve(), expected)
        self.assertEqual(marker.read_text(encoding="utf-8"), "preserve")

    def test_linked_out_is_refused_when_symlinks_are_available(self):
        outside = self.base / "outside"
        outside.mkdir()
        try:
            (self.repo / "out").symlink_to(outside, target_is_directory=True)
        except (OSError, NotImplementedError) as error:
            self.skipTest(f"Directory symlinks are unavailable: {error}")
        with self.assertRaises(RuntimeError):
            runtime.new_run(self.repo, "fpt-inspection")
        self.assertEqual(list(outside.iterdir()), [])

    def test_windows_reparse_attribute_is_refused(self):
        class FakeJunction:
            def lstat(self):
                return SimpleNamespace(st_file_attributes=0x400)

            def is_symlink(self):
                return False

        with self.assertRaises(RuntimeError):
            runtime._no_reparse(FakeJunction())

    def test_clone_rejects_windows_and_posix_path_components(self):
        work = self.base / "work"
        work.mkdir()
        for name in ("..", "../escape", "..\\escape", "C:relative"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                runtime.clone_project(self.repo, work, name)

    def test_selection_pointer_cannot_escape_out(self):
        run = runtime.new_run(self.repo, "fpt-inspection")
        pointer = run.parent / "latest-selection.json"
        pointer.write_text(json.dumps({
            "schema": "magiccontrol-fpt-selection-v1",
            "run": "run-..\\escape",
            "sha256": "0" * 64,
        }), encoding="utf-8")
        with self.assertRaises(RuntimeError):
            runtime.selected_functions(self.repo, "fpt-inspection", self.base / "fallback")
        with self.assertRaises(ValueError):
            runtime.selected_functions(self.repo, "../escape", self.base / "fallback")


if __name__ == "__main__":
    unittest.main()
