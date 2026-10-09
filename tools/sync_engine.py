"""SCRP dependency bootstrap. Games pin an exact commit in scrp.lock.json."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REPOSITORY = "https://github.com/Torward1024/SCRP.git"


def git(*args, cwd=None, check=True):
    result = subprocess.run(["git", *map(str, args)], cwd=cwd, text=True,
                            encoding="utf-8", errors="replace", capture_output=True)
    if check and result.returncode:
        raise RuntimeError(result.stderr.strip() or f"git exited {result.returncode}")
    return result


def validate_engine(path):
    for name in ("CMakeLists.txt", "include/scrp/Json.h", "src/Json.cpp", "src/Vfs.cpp"):
        if not (path / name).is_file():
            raise RuntimeError(f"Not a SCRP source directory: {path} (missing {name})")
    return path.resolve()


def clean(path):
    return not git("status", "--porcelain", "--untracked-files=normal", cwd=path).stdout.strip()


def matches(path, revision):
    if not (path / ".git").exists():
        return False
    result = git("rev-parse", "HEAD", cwd=path, check=False)
    return result.returncode == 0 and result.stdout.strip() == revision and clean(path)


def resolve(project, lock, override=None, offline=False):
    if override:
        path = validate_engine(Path(override).expanduser())
        print(f"[SCRP] local development override: {path}", file=sys.stderr)
        return path
    revision = lock["revision"]
    sibling = project.parent / "SCRP"
    if matches(sibling, revision):
        print(f"[SCRP] shared checkout {revision[:12]}: {sibling}", file=sys.stderr)
        return validate_engine(sibling)
    # Only this ignored checkout is managed. The sibling developer checkout is never changed.
    cache = project / ".deps" / "SCRP"
    cache.parent.mkdir(parents=True, exist_ok=True)
    if cache.exists():
        if not (cache / ".git").is_dir():
            raise RuntimeError(f"Dependency path is not a managed Git clone: {cache}")
        origin = git("remote", "get-url", "origin", cwd=cache).stdout.strip()
        if origin != lock["repository"]:
            raise RuntimeError(f"Unexpected repository in {cache}; refusing to change it")
        if not clean(cache):
            raise RuntimeError(f"Local changes in {cache}; preserve them before syncing")
    else:
        if offline:
            raise RuntimeError("SCRP is not available locally; run synchronization online once")
        git("clone", lock["repository"], cache)
    available = git("cat-file", "-e", revision + "^{commit}", cwd=cache, check=False).returncode == 0
    if not available:
        if offline:
            raise RuntimeError(f"SCRP commit {revision} is not cached")
        git("fetch", "--no-tags", "origin", revision, cwd=cache)
    # A generated cache may be detached. A developer's E:/Dev/SCRP never is.
    if git("rev-parse", "HEAD", cwd=cache, check=False).stdout.strip() != revision:
        git("checkout", "--detach", revision, cwd=cache)
    if not matches(cache, revision):
        raise RuntimeError("SCRP checkout does not match the lock file")
    print(f"[SCRP] pinned cache {revision[:12]}: {cache}", file=sys.stderr)
    return validate_engine(cache)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--ensure", action="store_true", help="ensure the pinned revision (default)")
    mode.add_argument("--update", action="store_true", help="explicitly adopt the latest remote main")
    parser.add_argument("--engine-dir", help="use local development sources without synchronization")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--print-path", action="store_true", help="stdout contains only the source directory")
    args = parser.parse_args()
    project = args.project.resolve()
    lock_path = project / "scrp.lock.json"
    lock = json.loads(lock_path.read_text(encoding="utf-8-sig"))
    if (lock.get("schema_version") != 1 or lock.get("repository") != REPOSITORY or
            not re.fullmatch(r"[0-9a-f]{40}", lock.get("revision", ""))):
        raise RuntimeError(f"Invalid SCRP lock file: {lock_path}")
    override = args.engine_dir or os.environ.get("SCRP_ENGINE_DIR")
    if args.update:
        if args.offline or override:
            parser.error("--update requires online mode and no local override")
        remote = git("ls-remote", "--exit-code", lock["repository"], "refs/heads/main").stdout.split()
        if not remote or not re.fullmatch(r"[0-9a-f]{40}", remote[0]):
            raise RuntimeError("Remote main did not return a commit")
        updated = dict(lock, revision=remote[0])
        path = resolve(project, updated)
        temporary = lock_path.with_suffix(".json.tmp")
        temporary.write_text(json.dumps(updated, indent=2) + "\n", encoding="utf-8")
        temporary.replace(lock_path)
        print(f"[SCRP] lock updated to {updated['revision']}; build/test before committing", file=sys.stderr)
    else:
        path = resolve(project, lock, override, args.offline)
    print(path.as_posix() if args.print_path else f"SCRP ready: {path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f"SCRP synchronization failed: {error}", file=sys.stderr)
        sys.exit(1)
