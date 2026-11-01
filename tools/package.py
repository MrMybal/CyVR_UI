"""Produce source and Windows archives with an explicit allowlist, excluding local keys/caches."""
from pathlib import Path
import zipfile

root = Path(__file__).resolve().parents[1]
dist = root / "dist"
dist.mkdir(exist_ok=True)
examples = {"design/reference.cyvr.json", "design/layout.schema.json"}
with zipfile.ZipFile(dist / "CyVR_UI-Windows.zip", "w", zipfile.ZIP_DEFLATED) as out:
    for path in sorted((dist / "windows").rglob("*")):
        relative = path.relative_to(dist / "windows")
        allowed = relative.as_posix() in examples or relative.as_posix() in {"CyVR_UI.exe", "CyVR_Editor.exe"} or relative.parts[0] in {"assets", "licenses"}
        if path.is_file() and allowed and relative.parts[:2] != ("assets", "branding") and path.name not in {"preferences.txt", "diagnostic.json"}:
            out.write(path, relative)

files = {"CMakeLists.txt", "README.md", "LICENSE", "THIRD_PARTY.md", "build.ps1", ".gitignore", ".gitattributes", ".clang-format"}
folders = {"include", "src", "shaders", "assets", "android", "tests", "tools", "docs", "third_party", "design"}
with zipfile.ZipFile(dist / "CyVR_UI-Source.zip", "w", zipfile.ZIP_DEFLATED) as out:
    for path in sorted(root.rglob("*")):
        relative = path.relative_to(root)
        if not path.is_file() or relative.parts[:2] == ("assets", "branding") or any(part in {".cache", ".deps", ".git", "build", "dist", "__pycache__"} for part in relative.parts):
            continue
        if relative.parts[0] == "design" and relative.as_posix() not in examples:
            continue
        if relative.parts[0] in folders or str(relative) in files:
            out.write(path, relative)
print("Archives ready in", dist)
