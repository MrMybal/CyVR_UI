# Immersive environments

The bundled 4096 × 2048 panoramas are photographic, tonemapped derivatives of:

| File | Author | Source | License |
|---|---|---|---|
| `lebombo.jpg` | Greg Zaal | https://polyhaven.com/a/lebombo | CC0-1.0 |
| `venice_sunset.jpg` | Greg Zaal | https://polyhaven.com/a/venice_sunset | CC0-1.0 |

Source assets are provided by [Poly Haven](https://polyhaven.com/), under its [CC0 asset license](https://polyhaven.com/license). The CC0 legal text is included in `third_party/notices/CC0-1.0.txt`. These assets can be redistributed in open-source and commercial applications under those terms. The project code remains MIT; the panorama assets retain CC0.

`tools/prepare_scenes.py` verifies the original tonemapped JPEG hashes, resizes them to 4K and exports pixels without camera metadata. Prepared images are included, so normal builds need no Pillow, downloads or network access for environments. The app loads local JPEGs with stb_image, licensed under MIT (upstream dual MIT/public-domain license).

These are monoscopic 360-degree backgrounds: head rotation changes the view, while translation does not produce photographic scene parallax. Panels and rendered hands remain independent stereo 3D content with real depth. The renderer retains a lightweight procedural fallback for apps that do not bundle these optional assets. No HDR runtime exposure or geometric reconstruction is implied by these tonemapped backgrounds.
