"""Prepare bundled 4K CC0 environments. Pillow is a build-time tool only."""
from pathlib import Path
import hashlib
import urllib.request
from PIL import Image

root = Path(__file__).resolve().parents[1]
sources = {
    "lebombo": "2e70777b9947b8a74809c22b9485bea6c4d67955e1ab2747507890b429eb1e9b",
    "venice_sunset": "1fde00d96f688a4a10cf76781d7e8f51295819d7ef242a74d85f029ddd9346be",
}
cache = root / ".deps/scene-sources"
cache.mkdir(parents=True, exist_ok=True)
output = root / "assets/scenes"
output.mkdir(parents=True, exist_ok=True)
for name, digest in sources.items():
    source = cache / (name + ".jpg")
    if not source.exists():
        url = "https://dl.polyhaven.org/file/ph-assets/HDRIs/extra/Tonemapped%20JPG/" + name + ".jpg"
        request = urllib.request.Request(url, headers={"User-Agent": "CyVR-UI offline asset preparation"})
        with urllib.request.urlopen(request, timeout=30) as response:
            source.write_bytes(response.read())
    if hashlib.sha256(source.read_bytes()).hexdigest() != digest:
        raise RuntimeError("Panorama source checksum mismatch: " + name)
    with Image.open(source) as image:
        if image.width != image.height * 2:
            raise RuntimeError("Expected a 2:1 equirectangular panorama")
        # Export pixels only: original camera/GPS metadata is not copied into runtime assets.
        image = image.convert("RGB").resize((4096, 2048), Image.Resampling.LANCZOS)
        image.save(output / (name + ".jpg"), quality=95, subsampling=0, optimize=True)
    print("Prepared:", name, "4096 x 2048 / CC0")
