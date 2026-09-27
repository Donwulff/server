#!/usr/bin/env python3
"""Check each local topic against release tags without changing any worktree.

Requires Git with merge-tree --write-tree and --merge-base support.
This checks textual applicability, not compilation or runtime behavior.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


TOPICS = (
    ("stats-zero-window", "9165eb215f471948b84d31cac3add69cac22c4ea"),
    ("mdev-26123", "089641756a9eba87b51e07203b10c6f0e875c6c8"),
    ("spatial-interiorring-wkb", "c4fa0640038c62ec8d6099d0cf54a8c0b83d5bc5"),
    ("mdev-38372-defense", "50196ab8582c2e5dd0ab4e4026f9c25510f0050f"),
    ("mdev-24303", "4d81b6b5950e10aa2b2e4e76d00bb910d3f3b0f0"),
)
SOURCE_BASE = "45717ef4c736a6a92e0acf49399ce677bfcaea89"
DEFAULT_TARGETS = (
    "mariadb-10.11.19", "mariadb-11.4.13", "mariadb-11.8.9",
    "mariadb-12.3.3", SOURCE_BASE,
)


def git(*args, **kwargs):
    return subprocess.run(
        ["git", *args], text=True, capture_output=True, **kwargs
    )


def main():
    os.chdir(Path(__file__).resolve().parent.parent)
    objects = git("rev-parse", "--git-path", "objects", check=True)
    objects = str(Path(objects.stdout.strip()).resolve())
    print("# Local patch applicability\n")
    print("Each topic is checked independently against the unpatched target.")
    print("Clean three-way merges still require compilation and MTR tests.\n")
    print("| Target (commit) | Topic | Direct apply | Three-way merge |")
    print("| --- | --- | --- | --- |")
    conflicts = []
    with tempfile.TemporaryDirectory(prefix="mariadb-apply-") as tmp:
        env = os.environ.copy()
        env["GIT_INDEX_FILE"] = str(Path(tmp) / "index")
        env["GIT_OBJECT_DIRECTORY"] = str(Path(tmp) / "objects")
        Path(env["GIT_OBJECT_DIRECTORY"]).mkdir()
        env["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = objects
        for target in sys.argv[1:] or DEFAULT_TARGETS:
            commit = git("rev-parse", target + "^{commit}", check=True)
            commit = commit.stdout.strip()
            for name, topic in TOPICS:
                base = git("merge-base", SOURCE_BASE, topic, check=True)
                base = base.stdout.strip()
                patch = git("diff", "--binary", base, topic, check=True)
                git("read-tree", commit, env=env, check=True)
                direct = git("apply", "--cached", "--check", env=env,
                             input=patch.stdout)
                merge = git("merge-tree", "--write-tree", "--name-only",
                            "--merge-base=" + base, commit, topic, env=env)
                if merge.returncode not in (0, 1):
                    raise RuntimeError(merge.stderr or merge.stdout)
                clean = merge.returncode == 0
                print(f"| {target} ({commit[:12]}) | {name} | "
                      f"{'clean' if direct.returncode == 0 else 'no'} | "
                      f"{'clean' if clean else 'conflicts'} |")
                if not clean:
                    paths = merge.stdout.split("\n\n", 1)[0].splitlines()[1:]
                    conflicts.append((target, name, paths))
    if conflicts:
        print("\nConflicting paths:\n")
        for target, name, paths in conflicts:
            print(f"- {target}, {name}: " + ", ".join(paths))


if __name__ == "__main__":
    main()
