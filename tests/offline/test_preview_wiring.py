"""Fail if either actual KEYs page stops forwarding the selected model.

The executable matrix exercises real core behavior; this small source-boundary
check covers the two UI constructor call sites without starting either GUI.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[2]
for platform in ("windows", "linux"):
    page = (root / "src" / "gui" / platform / "KeysPage.cpp").read_text(encoding="utf-8")
    factory = (root / "src" / "platform" / platform /
               ("KeysHardwareWindows.cpp" if platform == "windows" else "KeysControlLinux.cpp")).read_text(encoding="utf-8")
    start = page.find("createKeysOperations(")
    if start < 0:
        raise SystemExit(f"{platform}: KEYs page factory call missing")
    call = page[start:start + 2500]
    if not re.search(r"deviceProfile\s*\(\s*ui::presentationBoard\s*\(\s*\)\s*\)\s*\.\s*id", call):
        raise SystemExit(f"{platform}: selected presentation profile not forwarded to KEYs owner")
    if "operations->visibleRows()" not in page or "operations->visible(" not in page:
        raise SystemExit(f"{platform}: KEYs rendering does not consume core visibility")
    if not re.search(r"owner->profile\s*=\s*deviceProfile\s*\(\s*readBoardDescriptor\s*\(\s*\)\s*\)\s*\.\s*id", factory):
        raise SystemExit(f"{platform}: hardware owner no longer derives the actual board")
    if not re.search(r"make_unique\s*<\s*KeysOperations\s*>\s*\(\s*owner->profile\s*,[^;]*presentationProfile", factory, re.S):
        raise SystemExit(f"{platform}: factory does not forward actual and presentation profiles to core")
print("Both KEYs pages forward selected profile for display and retain actual hardware identity")
