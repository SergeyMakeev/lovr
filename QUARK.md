# Quark runtime fork (Windows)

This repository is a fork of [LÖVR](https://github.com/bjornbytes/lovr) used as a lightweight portable runtime for [Quark](https://github.com/) games on **Windows**. It is **not** intended to track upstream API compatibility.

## Material overrides

Quark’s binding applies per–glTF-material overrides through the instance API:

- `Model:setMaterialOverride(materialIndex, material)` — `materialIndex` is **1-based** (same as `Model:getMaterial`). Pass `nil` to clear.
- `Model:getMaterialOverride(materialIndex)` — returns the override `Material` or `nil`.

Overrides are resolved inside `drawNode` / `lovrPassDrawPart` / `lovrModelGetMesh` via `lovrModelResolvePartMaterial`, so **`pass:draw(model)`** uses the correct materials for skinned rigs without a separate submesh pass.

Implementation: `materialOverrides` array on `struct Model` (parallel to `materials`), set in `graphics.c`; Lua bindings in `src/api/l_graphics_model.c`.

## Building (Windows)

Use the upstream LÖVR build instructions (CMake + MSVC or MinGW). Replace Quark’s `tools/lovr-win/lovr.exe` (or your pinned path) with the built binary.
