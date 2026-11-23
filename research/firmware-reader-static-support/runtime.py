"""Isolated working paths for offline firmware-reader analysis scripts.

This module only copies existing Ghidra project files. It does not execute
the analysed executable, interact with devices, or start Ghidra by itself.
"""

from pathlib import Path, PureWindowsPath
import hashlib
import json
import shutil
import tempfile


def _single_name(value: str) -> None:
    if (not isinstance(value, str) or not value or value in {".", ".."}
            or any(char in value for char in ("/", "\\", ":", "\0"))
            or Path(value).name != value or PureWindowsPath(value).drive):
        raise ValueError("Expected one path component")


def _no_reparse(path: Path) -> None:
    info = path.lstat()
    if path.is_symlink() or getattr(info, "st_file_attributes", 0) & 0x400:
        raise RuntimeError(f"Ghidra project contains a link: {path}")


def new_run(repo: Path, label: str) -> Path:
    """Create a unique run beneath out without following a linked component."""
    _single_name(label)
    base = Path(repo)
    _no_reparse(base)
    for component in ("out", "firmware-readers", label):
        base = base / component
        try:
            base.mkdir()
        except FileExistsError:
            pass
        _no_reparse(base)
        if not base.is_dir():
            raise NotADirectoryError(str(base))
    return Path(tempfile.mkdtemp(prefix="run-", dir=base))


def clone_project(saved: Path, work: Path, name: str) -> None:
    """Copy one preserved project into a fresh run before Ghidra opens it.

    Ghidra can update indexes even with -readOnly. The saved project therefore
    must never be opened in place by these repeatable research scripts.
    """
    _single_name(name)
    _no_reparse(saved)
    marker = saved / (name + ".gpr")
    repository = saved / (name + ".rep")
    _no_reparse(marker)
    _no_reparse(repository)
    if not marker.is_file() or not repository.is_dir():
        raise FileNotFoundError(f"Saved Ghidra project is incomplete: {saved / name}")
    for path in repository.rglob("*"):
        _no_reparse(path)
    if any(work.iterdir()):
        raise FileExistsError(f"Working project directory is not empty: {work}")
    shutil.copy2(marker, work / marker.name)
    shutil.copytree(repository, work / repository.name)


def publish_selection(run: Path) -> None:
    """Publish the latest completed inspection without overwriting its output."""
    selected = run / "selected-functions.txt"
    _no_reparse(selected)
    payload = {
        "schema": "magiccontrol-fpt-selection-v1",
        "run": run.name,
        "sha256": hashlib.sha256(selected.read_bytes()).hexdigest(),
    }
    pointer = run.parent / "latest-selection.json"
    try:
        pointer.lstat()
    except FileNotFoundError:
        pass
    else:
        _no_reparse(pointer)
        old = json.loads(pointer.read_text(encoding="utf-8"))
        if old.get("schema") != payload["schema"]:
            raise RuntimeError(f"Refusing to overwrite an unknown selection file: {pointer}")
    temporary = run / "latest-selection.tmp"
    with temporary.open("x", encoding="utf-8") as stream:
        json.dump(payload, stream)
        stream.write("\n")
    temporary.replace(pointer)


def selected_functions(repo: Path, label: str, fallback: Path) -> Path:
    """Use the last verified inspection, or the saved historical selection."""
    _single_name(label)
    base = repo / "out" / "firmware-readers" / label
    pointer = base / "latest-selection.json"
    try:
        pointer.lstat()
    except FileNotFoundError:
        return fallback
    _no_reparse(pointer)
    data = json.loads(pointer.read_text(encoding="utf-8"))
    name = data.get("run")
    if data.get("schema") != "magiccontrol-fpt-selection-v1" or not isinstance(name, str) or not name.startswith("run-"):
        raise RuntimeError(f"Invalid selection pointer: {pointer}")
    try:
        _single_name(name)
    except ValueError as error:
        raise RuntimeError(f"Invalid selection pointer: {pointer}") from error
    selected = base / name / "selected-functions.txt"
    _no_reparse(selected)
    if hashlib.sha256(selected.read_bytes()).hexdigest() != data.get("sha256"):
        raise RuntimeError(f"Selection changed after publication: {selected}")
    return selected
