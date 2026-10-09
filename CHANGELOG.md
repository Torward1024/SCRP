# История изменений

## [0.1.0] — 2026-10-09

- SCRP выделен в собственный репозиторий из FirstDawn/engine.
- JSON/VFS извлечены из Scrapheart с сохранением формата пакетов SCRP v1.
- Добавлены индексные изображения, BMP-экспорт и отдельный SDL2-модуль окна.
- Добавлены независимые сборки Core, CMake targets и синтетические тесты.
- Добавлен bootstrap для закреплённых зависимостей игр и тесты его поведения.

Происхождение JSON/VFS: Scrapheart commit
1941a7f01d2a8784ca9c9aafbfb6c1574c8f78ef, затем выделение namespace и
усиление проверок JSON/пакетов в FirstDawn. Оригинальных игровых ресурсов нет.


## 0.2.0

- Extracted the reusable C++17 runtime: configuration/manifests, animation,
  events/signals/stats, grid collision/navigation, atomic saves and fixed steps.
- Added SDL sprite roles, drawing/camera, input, lighting, effects, decals and windows.
- Added optional audio with safe registry reload and owned music buffers.
- Game bindings and resource mounts are supplied through consumer JSON.
