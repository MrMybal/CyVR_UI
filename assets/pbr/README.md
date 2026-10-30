# Original PBR materials and lighting

All files in this directory and their generator are original CyVR creations, distributed under the project's [MIT license](../../LICENSE). No third-party textures, models, photographs or logo artwork are used here. The optional CC0 photographic panoramas remain separate, in `assets/scenes/`.

The reference renderer uses a metallic/roughness model: GGX distribution, correlated Smith visibility, Schlick Fresnel and diffuse energy sharing. Its mathematical references are the [Khronos glTF material BRDF specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#appendix-b-brdf-implementation) and the discussion of [prefiltered IBL and split-sum integration in Filament](https://google.github.io/filament/Filament.html). No glTF importer or external rendering engine is embedded.

## Included resources

| File | Format / channels | Dimensions |
|---|---|---|
| `basecolor.bin` | sRGB RGB modulation, linear alpha AO | 128 × 128 × 9, 8 mip levels |
| `normal-rough-metal.bin` | Linear UNORM: tangent normal XY, perceptual roughness, metallic | 128 × 128 × 9, 8 mip levels |
| `brdf.bin` | Linear RG16F: integrated specular scale and bias | 64 × 64 |
| `lounge.bin`, `observatory.bin`, `terrace.bin` | Linear HDR RGBA16F cube maps, GGX-prefiltered by roughness | 6 × 128 × 128, 8 mip levels each |
| `lighting-*.bin` | Offline vertex lighting: sun visibility, AO, indirect RGB | One record per scene vertex |

Material layers are matte (0), wood (1), fabric (2), metal (3), emissive (4), water (5), foliage (6), ceramic (7) and planet (8). Vertex colors provide the material tint and are converted from sRGB to linear before lighting. Normal-map Z is reconstructed as the positive hemisphere. The emissive shader bypasses the material lookups. The metal layer has metallic = 1; ordinary wood, cloth, water and ceramic use metallic = 0.

Material mips are averaged in linear space; unresolved normal variance widens roughness. HDR probes use 256 deterministic GGX importance samples per filtered texel, with roughness equal to mip index / 7. Direct sunlight is excluded from the probes because it is evaluated separately by the shader. The 64 × 64 BRDF table uses 512 samples per texel and the same Smith visibility convention as direct lighting.

The probes approximate each scene from a central location. They supply distant, static reflections; they are not exact room reflections, planar mirrors or ray-traced reflections. Nearby diffuse lighting, fixture contributions, triangle-based AO and color bounces are baked separately into the scene vertices. The original offline CPU baker uses a BVH of the actual scene, 512 cosine-weighted paths per unique surface sample, up to two diffuse surface interactions, 2,048 importance-sampled emissive-area samples and 64 soft-sun samples. It filters residual noise only between nearby coplanar samples. The staged astronomical background receives distant lighting without foreground rail shadows. This is a finite-sample vertex bake; small shadow details are limited by geometry sampling, and it is not a high-resolution lightmap atlas. The reference renderer does not provide dynamic GI, shadow maps or reflections of moving hands/windows.

Only the material arrays, integration table and **one** probe are resident: 2,637,784 bytes of texel data (about 2.52 MiB), plus driver allocation overhead. Scene geometry has a separate budget. Vertex-lighting files are merged into that existing geometry buffer; they add no texture or per-frame ray-tracing pass. Native 2D does not allocate these textures.

## Rebuild and verify

The generated binaries are committed so normal app builds do not require NumPy or a baking step.

```powershell
python -m pip install -r tools/requirements-pbr.txt
python tools/prepare_pbr.py
python tools/prepare_pbr.py --check
# After geometry or static-light changes, rebuild the original offline triangle bake:
.\tools\bake-lighting.ps1 -Samples 512
```

No network requests are made by the generator or the app. `SHA256SUMS` records all nine generated files. `BAKE_SOURCES.sha256` records the geometry and baker sources; normal hybrid builds reject stale lighting resources after those sources change. The PowerShell helper rebuilds the C++ baker, generates all three lighting files and records their checksums. NumPy is an authoring dependency only; checksum validation and the C++ lighting baker do not require it.

## Native app integration

Enable `CYVR_INCLUDE_PBR=ON` to copy the reference resources into the CMake output `assets/pbr` directory, or include these files in your own Android assets. Pass your `AssetReader` to `VulkanRenderer::setScene(background, anchor, reader)`. The reader is used synchronously only when a scene changes; it is not stored. Readers can redirect these asset names to custom resources that follow the same nine-layer layout. Absent optional files use small neutral textures; malformed non-empty texture data is rejected.

The standalone strictly 2D build disables `CYVR_INCLUDE_PBR` and excludes these files. Shared UI components, font/video bindings and the default Android panel presentation are independent of PBR lighting.

## Binary format

`CYTX` magic, followed by seven little-endian uint32 values: version (1), width, height, layers, mip count, format (1 = sRGBA8, 2 = linear RGBA8, 3 = linear RGBA16F, 4 = linear RG16F), cube flag (0 or 1). The 32-byte header is followed by tightly packed mip levels, each containing all layers in order. Cube faces follow Vulkan's +X, -X, +Y, -Y, +Z, -Z order. Width/height halve at each mip, clamped to 1. `decodePbrTexture` validates dimensions, formats, cube constraints and exact byte length before GPU allocation.

The vertex-lighting container uses `CYLI` magic followed by uint32 version (1), background ID and vertex count. It then stores five little-endian float32 values per vertex: sun visibility, ambient visibility and linear indirect RGB. The loader validates scene identity, vertex count, exact length and finite nonnegative bounded values before modifying the mesh.
