# SCRP

SCRP is my C++17 / SDL2 engine for recreating classic games. The name comes from
**scrap**: fragments of something old and worn. I extracted the shared systems
from Scrapheart and connected FirstDawn to the same engine.

[Torward1024/SCRP](https://github.com/Torward1024/SCRP) is the single source repository
for the engine. Game rules and original resources belong to the consumer projects.
Resource bindings and runtime settings are supplied through JSON.

## Modules

| Target | Features | Dependencies |
|---|---|---|
| `SCRP::Core` | JSON/XML/VFS, configuration, indexed images/BMP, vectors/RNG, animation, events/signals/stats, grid collision/visibility, flow fields/A*, saves, fixed steps | C++17 |
| `SCRP::SDL2` | Windows, pixel output, sprite registries/roles, drawing/camera, input, microfont, lighting, particles and decals | SDL2; optional SDL2_image |
| `SCRP::Audio` | Sound registry, channels/priorities, spatial audio, ambient loops and music | SDL2; optional SDL2_mixer |

Core has no SDL or game dependencies. A game supplies its solid grid, action names
and JSON settings. The engine does not know specific enemies, weapons, keys or
game files. Resource mounts and SDL objects are managed on the main thread.

## Layout

```text
include/scrp/       public API
src/               shared implementations
tests/             independent Core, SDL and bootstrap tests
tools/             consumer dependency synchronization
docs/              architecture and integration
build.ps1          Windows Core build and tests
CMakeLists.txt     portable module builds
```

## Build

Windows uses MSYS2 UCRT64, as do Scrapheart and FirstDawn.

```powershell
.\build.ps1
.\build.ps1 -Test
.\build.ps1 -Test -SDLTest
```

The last command tests SDL2 and SDL2_mixer without a visible window or original
game assets. CMake lets consumers select optional dependencies:

```sh
cmake -S . -B build/cmake -DSCRP_WITH_SDL2=ON -DSCRP_USE_SDL_MIXER=ON
cmake --build build/cmake
ctest --test-dir build/cmake --output-on-failure
```

Add `-DSCRP_USE_SDL_IMAGE=ON` for PNG sprites. Use `-DSCRP_WITH_SDL2=OFF` for tools
that need only Core. CI tests the engine and bootstrap on Windows and Linux.

## Integration

```cmake
add_subdirectory("${SCRP_ENGINE_DIR}" scrp)
target_link_libraries(my_game PRIVATE SCRP::Core SCRP::SDL2)
# Link SCRP::Audio when audio is needed.
```

Each game pins a full commit SHA in `scrp.lock.json`. Normal builds automatically
resolve that commit into an ignored `engine/SCRP/` clone. Games contain no separate
engine implementation. To adopt the latest remote main, run from the game root:

```sh
python tools/sync_engine.py --update
```

Then build/test the consumer and commit its lock file. To test local engine work
in `E:\Dev\SCRP`, explicitly use `-EngineDir` or `SCRP_ENGINE_DIR`. Normal builds
never select or modify a sibling development checkout.

See [architecture and JSON](docs/ARCHITECTURE.md) and
[dependency versions](docs/DEPENDENCIES.md). The 0.2 API is evolving. I add shared
capabilities here and game rules in the corresponding consumer project.
