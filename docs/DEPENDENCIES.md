# SCRP integration and versions

Each game keeps `scrp.lock.json` with `schema_version: 1`, the SCRP repository URL
and a full commit SHA. This repository maintains the canonical `tools/sync_engine.py`.
Both consumers carry identical bootstrap copies so their first build can fetch SCRP.

## Normal builds

1. An explicit `--engine-dir` / `SCRP_ENGINE_DIR` uses development sources directly.
2. Without an override, synchronization creates or validates the game's ignored
   `engine/SCRP/` clone.
3. A missing commit is downloaded and the managed clone checks out the pinned SHA.
4. Local cache changes or a foreign origin cause an error without overwriting files.

The sibling development checkout is never selected or changed automatically.
Once the commit is cached, a build needs no network. `--offline` forbids downloads
and reports missing dependencies. Normal builds neither change the lock file nor
follow the moving main branch.

## Development and updates

```powershell
# Test uncommitted engine work from the consumer directory:
.\build.ps1 -EngineDir E:\Dev\SCRP
```

CMake uses `-DSCRP_ENGINE_DIR=...`; Make uses `SCRP_ENGINE_DIR=...`. Test Core/SDL
and both consumers, publish the engine commit, then run in each consumer:

```powershell
python tools/sync_engine.py --update
.\build.ps1 -Test
.\build.ps1
```

`--update` resolves remote main, ensures its checkout, then atomically replaces the
lock file. Consumer tests are run separately. Games can update independently.
Do not edit `engine/SCRP/` manually: shared changes belong to this source repository.
