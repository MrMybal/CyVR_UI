"""Produce source and Windows archives with an explicit allowlist, excluding local keys/caches."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root / "dist"
dist.mkdir(exist_ok=True)
with zipfile.ZipFile(dist / "CyVR_UI-Windows.zip", "w", zipfile.ZIP_DEFLATED) as out:
    for path in sorted((dist / "windows").rglob("*")):
        if path.is_file() and path.name not in {"preferences.txt", "diagnostic.json"}:
            out.write(path, path.relative_to(dist / "windows"))

files = {"CMakeLists.txt", "README.md", "LICENSE", "THIRD_PARTY.md", "build.ps1", ".gitignore", ".gitattributes", ".clang-format"}
folders = {"include", "src", "shaders", "assets", "android", "tests", "tools", "docs", "third_party"}
with zipfile.ZipFile(dist / "CyVR_UI-Source.zip", "w", zipfile.ZIP_DEFLATED) as out:
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or any(part in {".cache", ".deps", ".git", "build", "dist", "__pycache__"} for part in relative.parts):
            continue
        if relative.parts[0] in folders or str(relative) in files:
            out.write(path, relative)
print("Archives ready in", dist)
