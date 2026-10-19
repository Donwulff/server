# Local MariaDB maintenance

This directory records the local patch set and its supported targets. Keep
development on the HEAD topic branches, assemble them in `integration`, and
backport with `git cherry-pick -x` to `maint/10.11`. Keep upstream `main`
unmodified so it remains a convenient reference.

## Targets (checked 2026-09-27)

| Role | Branch | Worktree relative to the main checkout | Upstream base |
| --- | --- | --- | --- |
| Upstream reference | `main` | `.` | `45717ef4c736` |
| Combined HEAD fixes | `integration` | `.claude/worktrees/integration` | `45717ef4c736` |
| Production backport | `maint/10.11` | `.claude/worktrees/maint-10.11` | `mariadb-10.11.19` (`93e051860a9c`) |

The worktree locations preserve this checkout's existing convention; they
are ordinary Git worktrees and do not depend on any particular editor.
Each worktree has its own ignored `build/` and test data. Never reuse a
production data directory for MTR or between major versions.

MariaDB 10.11 is the oldest **Community** series still receiving maintenance
and security fixes, through 16 February 2028. Community 10.6 reached end of
life on 6 July 2026. Paid Enterprise support has different dates. The
production branch starts at the latest published 10.11 release, rather than
unreleased commits on the upstream `10.11` branch.

Sources:

- [MariaDB maintenance policy](https://mariadb.org/about/#maintenance-policy)
- [Q3 2026 maintenance releases](https://mariadb.org/mariadb-server-12-3-11-8-11-4-and-10-11-q3-2026-maintenance-releases-and-goodbye-10-6/)

## Patch ownership

| Topic branch | Change |
| --- | --- |
| `worktree-stats-zero-window` | Preserve visible index statistics during recalculation |
| `worktree-mdev-26123` | Permit spatial index reads in an already-started transaction |
| `worktree-spatial-interiorring-wkb` | Bound malformed `ST_InteriorRingN()` parsing |
| `worktree-mdev-38372-defense` | Validate truncated nested GIS WKB in InnoDB |
| `worktree-mdev-24303` | Atomic statistics countdown, persistent progress, regression tests |

The initial source series is pinned in [source-series.txt](source-series.txt).
The `worktree-binlog-compress-filter` branch has no local fixes in the
current integration and is not part of this backport. The old
`integration-rebuilt-20260831` branch is historical; use `integration` now.

Every backported source commit has a `(cherry picked from commit ...)`
trailer. Use that trailer, rather than matching hashes or merging entire
branches across major versions, to track which fixes are present:

```sh
git log --reverse --format='%h %s%n%b' mariadb-10.11.19..maint/10.11
git diff --stat main...integration
git diff --stat mariadb-10.11.19..maint/10.11
```

The 10.11 adaptations preserve its older InnoDB statistics signatures,
defragmentation statistics, schema-name representation, and recovery API.
System-table scripts are still named `mysql_system_tables*.sql` there.
Expected results retain the 10.11 metadata columns and SQL output.

## Adding a fix

1. Develop and test it on the appropriate HEAD topic branch.
2. Integrate the tested change into `integration` using the existing topic
   workflow. If topics were rebased, rebuild integration from current
   `main` and the current topics instead of merging obsolete topic history.
   Also carry the integration-only upgrade regression (`90727457090`) and
   the separate maintenance documentation commits when rebuilding.
3. In the 10.11 worktree, run `git cherry-pick -x <source-commit>` for each
   new commit in dependency order. Resolve against 10.11's APIs, then build
   and run the affected MTR tests.
4. Update the pinned source series, applicability report, and validation
   notes. Develop shared corrections on HEAD first, then backport them;
   keep adaptations unique to 10.11 on the maintenance branch.

Keep maintenance documentation and tools in separate commits from server
fixes, so they can be omitted from an eventual upstream submission. HEAD
is the development reference, but upstream's `CODING_STANDARDS.md` directs
bug fixes to the oldest affected maintained branch no more than three
years past GA. Recheck the requested submission branch when submitting.

## Updating to a new 10.11 release

From the maintenance worktree, with a clean worktree and after reviewing
the release notes (replace the example tag and backup name):

```sh
git fetch mariadb tag mariadb-10.11.NEXT
git branch backup/maint-10.11-before-NEXT
git rebase --onto mariadb-10.11.NEXT mariadb-10.11.19 maint/10.11
git submodule update --init --recursive libmariadb wsrep-lib \
  storage/maria/libmarias3
```

The rebase replays only the local linear series onto the new release. Check
upstream for equivalent fixes and remove only patches actually superseded.
Retain the backup branch until validation is complete. Update the old base
tag in these notes after each refresh. Coordinate history rewriting if the
branch is later shared; do not force-push it implicitly.

## Build and validation

The 10.11 debug build uses the existing system dependencies and selected
submodules, with RocksDB and ColumnStore disabled:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DUPDATE_SUBMODULES=OFF -DWITH_SSL=system \
  -DWITH_EMBEDDED_SERVER=OFF -DWITH_LIBAIO=OFF -DWITH_URING=OFF \
  -DPLUGIN_ROCKSDB=NO -DPLUGIN_COLUMNSTORE=NO
cmake --build build --target minbuild udf_example auth_test_plugin \
  type_mysql_timestamp -j6
cd build/mysql-test
perl mariadb-test-run.pl --parallel=4 \
  innodb.stats_zero_window innodb_gis.mdev-26123 \
  main.gis-malformed-wkb innodb_gis.rtree_malformed_wkb \
  innodb.innodb-stats-modified-counter \
  innodb.innodb_stats_reanalysis_counter \
  innodb.innodb_stats_reanalysis_upgrade \
  main.mysqldump-system main.mysqldump
```

Run the same focused regressions on HEAD. Also exercise existing statistics
and schema tests when changing MDEV-24303. Exact results and limitations are
recorded in [validation.md](validation.md).

MDEV-24303 changes `mysql.innodb_table_stats`: after installing the patched
server on an existing installation, run the matching `mariadb-upgrade`
binary with `--force` even when the upstream version number is unchanged.
The dedicated `innodb_stats_reanalysis_upgrade` test covers adding the
column to an unpatched schema, retaining statistics, rerunning the upgrade,
and restarting. This is a local schema extension: returning to an
unpatched binary requires planning for the original stats-table schema.
The build and MTR runs here do not constitute a production workload or
backup/restore qualification.

## Intermediate releases

Run `python3 local-maintenance/check-apply.py` from either maintained
worktree. Optional arguments select other locally available release refs.
The checker writes only temporary Git objects and an index; it does not
change branches, tracked files, or submodules.

The [2026-09-27 report](applicability-2026-09-27.md) pins all source and
target revisions. Four of the five topics apply directly to 11.4.13,
11.8.9 and 12.3.3. MDEV-24303 needs resolution on all three. These are
textual checks of each topic independently, not build or test certification
of intermediate releases. Only HEAD and 10.11 are maintained here.

## Automated maintenance (2026-10-19)

`~/mariadb-notes/sync-fork.sh` now validates both HEAD and 10.11 by default.
The default remote push still updates only `origin/main`. Examples:

```sh
~/mariadb-notes/sync-fork.sh --no-push
~/mariadb-notes/sync-fork.sh --targets=10.11 --no-push
~/mariadb-notes/sync-fork.sh --targets=head --no-push
~/mariadb-notes/sync-fork.sh --maint-release=mariadb-10.11.NEXT --no-push
# Publish both combined branches and all five topics after validation:
~/mariadb-notes/sync-fork.sh --push-fixes --rewrite-fixes
```

Replace `NEXT` with a numeric release suffix. The maintenance target stays
on its current release unless `--maint-release` is supplied. It is never
rebased onto `main` or the unreleased upstream `10.11` branch. The script
prints the current base and newest fetched release. `--no-fetch` validates
against the existing local upstream refs without accessing upstream.

Integration rebuilds retain the worktree and its build cache, save a
`backup/integration-*` ref, and replay integration-only commits after the
current topics. The upgrade regression and original maintenance notes are
explicit recovery commits because the previous script had dropped them.
An existing merge/rebase or dirty worktree causes a failure, not an abort
of someone else's work. Concurrent script runs are rejected with `flock`.

`reviewed-heads.txt` records the HEAD commits reviewed against the 10.11
adaptations and fingerprints of their added/deleted lines. Rebase-only
context changes do not invalidate this record. Changed patches fail the
backport audit and prevent publishing; review the differences, cherry-pick
new fixes with `-x`, and run both versions' regressions before refreshing
the manifest. This is a review gate, not an automatic cross-version port.
The original `source-series.txt` remains the September provenance record.

Both combined branches run the five topics' regressions, the dedicated
schema upgrade test, and dump tests with their required plugins built.
10.11 also runs `main.mysql_upgrade`. Neither intermediate versions nor a
full MTR run are implied by the default focused selection.

`--push-fixes` publishes `integration`, `maint/10.11`, and all five topic
branches atomically only after both builds, tests and the backport audit
pass. It requires both targets, integration and tests. Rewritten histories
additionally require `--rewrite-fixes`, which uses exact remote tips read
before the run as force-with-lease expectations. Newer remote changes reject
the push. Neither option deletes remote branches or publishes backup refs.

The 2026-10-19 review found only upstream context/API changes since the
September backport: `IndexStats` and the `stats_method` parameter on HEAD,
renamed statistics schema types, and surrounding include changes. The
10.11 adaptations keep their original APIs; no corresponding server-code
change is needed there. The HEAD stats conflict was resolved retaining the
new API and the debug-sync regression.
