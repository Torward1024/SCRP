# Changelog

## Unreleased

- Serialize JSON values for application saves with bounded nesting and stable numeric formatting.
- Preserve exact unsigned 64-bit counters through decimal strings; distinguish explicit null fields from missing fields.
- Upload decoded indexed images into sprite registries; games own their decoders.
- Draw arbitrary atlas regions and capture rendered frames for local verification.

## 0.2.0 - 2026-10-09

- Extracted the reusable C++17 runtime: configuration/manifests, animation,
  events/signals/stats, grid collision/navigation, atomic saves and fixed steps.
- Added SDL sprite roles, drawing/camera, input, lighting, effects, decals and windows.
- Added optional audio with safe registry reload and owned music buffers.
- Game bindings and resource mounts are supplied through consumer JSON.
- Normal builds resolve pinned engine versions into each game's `engine/SCRP/`.
- Standardized documentation, source comments and diagnostics in English.

## 0.1.0 - 2026-10-09

- Created the standalone SCRP repository from FirstDawn/engine.
- Extracted JSON/VFS from Scrapheart, preserving the SCRP v1 package format.
- Added indexed images, BMP export and a separate SDL2 window module.
- Added independent Core builds, CMake targets and synthetic fixtures.
- Added pinned consumer dependency bootstrap and synchronization tests.

JSON/VFS originated in Scrapheart commit
1941a7f01d2a8784ca9c9aafbfb6c1574c8f78ef. Namespace extraction and stronger JSON/package
validation followed in FirstDawn. No original game resources are included.
