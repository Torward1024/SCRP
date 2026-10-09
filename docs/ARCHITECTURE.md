# Architecture and configuration

SCRP is a reusable C++17 runtime. `SCRP::Core` has no SDL or game dependencies;
`SCRP::SDL2` implements presentation; `SCRP::Audio` supplies optional SDL_mixer audio.

Core contains JSON/XML/VFS, layered configuration, manifests, indexed images,
vectors and deterministic random streams, typed animation/events/stats, signals,
solid-grid collision and visibility, flow fields/A*, save storage and fixed steps.
SDL2 contains resource registries, string-based sprite roles, keyboard bindings,
camera and drawing, diagnostic font, lighting, particle/sprite effects, decal
canvases with JSON recipes, and window ownership.

Games own rules, identifiers, format decoders, mission/entity schemas and content.
Adapters translate game enums into configured resource identifiers; the runtime
never includes a game header or names an enemy, weapon, sound event or key layout.

A root JSON manifest declares ordered resource mounts (`pack`, `packs`, `directory`)
and a logical engine configuration path. The manifest is read from the filesystem;
every other content file goes through VFS. Later mounts override earlier mounts.
Configuration object fields merge recursively, arrays/scalars replace earlier
values. A malformed layer rejects the configuration instead of applying it partly.

Pass configuration sections to each component before initialization. Assets accept
`registry`, `roles`, `color_key`; rendering accepts assets, shake/focus settings and
tile fallback colours. Input accepts action names mapped to SDL key-name arrays.
Audio accepts its registry, channels/sample/buffer settings, hearing radius and
fade durations. Game event and music bindings remain in the game's JSON.
Decals accept bounded canvas/fading settings and rectangle/scatter/splat recipes;
particles can emit recipes and deliver a string death tag to an application callback.
Lighting accepts glow resolution and Gaussian sigma. SaveStore receives a directory;
Grid implementations provide dimensions, cell size, solids and a revision counter.

SDL resources must shut down before their renderer and SDL subsystems. Cosmetic
systems accept an injected RNG so visual effects never change gameplay randomness.
Events emitted from callbacks are queued for the next dispatch; nested dispatch is
ignored and a reset cancels the active batch. SaveStore rejects path-like filenames
and never deletes an existing save when atomic replacement fails.

The example consumer configuration is maintained in Scrapheart, rather than
distributed as built-in game data in SCRP. Original game assets stay in consumers'
local resource directories and never enter this repository.
