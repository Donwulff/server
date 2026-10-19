# Validation, 2026-09-27

## Revisions

- Upstream HEAD: `45717ef4c736a6a92e0acf49399ce677bfcaea89` (13.2.0).
- Combined original HEAD fixes: `984787b95db6490acbc0e1ad766c96a33b9f56b6`.
- Shared upgrade regression: `90727457090` on integration.
- Backport base: `mariadb-10.11.19`, commit `93e051860a9c`.
- All 16 source commits in `source-series.txt` have provenance trailers in
  `maint/10.11`. The adaptation was reviewed with `git range-diff`.

## HEAD

Debug `minbuild`, `udf_example`, `auth_test_plugin`, and
`type_mysql_timestamp` targets built successfully with GCC 15.

| Run | Result |
| --- | --- |
| Original fix regressions plus dump | 7 passed |
| Upgrade and schema-sensitive regressions | 16 passed, including page-size combinations |
| All InnoDB tests matching `.*stats.*` | 32 passed |

The first run also selected `main.mysql_upgrade` (skips on preview server
versions) and `main.mysqldump-system` (initially skipped for a missing auth
test plugin). After building the plugin, `main.mysqldump-system` passed in
the schema run. The dedicated `innodb_stats_reanalysis_upgrade` passed
without depending on the preview-version-sensitive general upgrade test.

The new test removes `reanalysis_counter` to reproduce the original
six-column system table, runs the upgrade tool, checks that existing stats
survive with a default counter, runs ANALYZE, repeats the upgrade, and
restarts the server. Its initial test-cleanup failure was fixed by removing
the generated upgrade marker; subsequent runs passed all testcase checks.

Local logs:

- `/tmp/mariadb-head-build.log`
- `/tmp/mariadb-head-mtr.log`
- `/tmp/mariadb-head-schema-mtr.log`
- `/tmp/mariadb-head-stats-mtr.log`

## 10.11

The Debug build completed with GCC 15, including `minbuild`,
`udf_example`, `auth_test_plugin`, and `type_mysql_timestamp`. Required
submodules were checked out at the revisions recorded by the 10.11 tag.
The sandbox blocked the bundled fmt download, so the identical cached
`fmt-12.2.0.zip` from the HEAD build was reused; its MD5 matched the
upstream CMake checksum `6083253772ad29e73a4d5920be08918e`.

The initial combined run passed 21 of 24 cases. The three differences were
resolved in the 10.11-only commit `621ddb7ade0`:

- EXPLAIN adds `Using index` for the nonspatial covering index. The test
  still asserts the spatial R-tree range access and join order.
- The extended 10.11 statistics table has checksum `3706663717` in the
  dump regression; other system-table checksums remain the 10.11 values.
- The upgrade test must clean up `mysql_upgrade_info`, the name used by
  10.11, rather than HEAD's `mariadb_upgrade_info`.

All three adapted tests then passed. The general `main.mysql_upgrade`
test also passed, including its older-version system-table upgrade paths.
The full `innodb` selection matching `.*stats.*` passed all 32 executable
cases, including `innodb_defrag_stats`. The additional
`innodb_defrag_stats_many_tables` case was skipped because it requires
`--big-test`; that larger workload was not run.

The final clean combined run passed **all 24 cases**, including page-size
combinations, both dump tests, both upgrade tests, GIS regressions, and
schema checks. No cases were skipped in that run.

Local logs:

- `/tmp/mariadb-10.11-configure.log`
- `/tmp/mariadb-10.11-build.log`
- `/tmp/mariadb-10.11-mtr.log` (initial differences)
- `/tmp/mariadb-10.11-adapted-mtr.log`
- `/tmp/mariadb-10.11-stats-mtr.log`
- `/tmp/mariadb-10.11-final-mtr.log`

## Reproducing the broader runs

From `build/mysql-test` on either target:

```sh
perl mariadb-test-run.pl --parallel=4 --suite=innodb --do-test='.*stats.*'
```

The full 10.11 combined run (HEAD skips `main.mysql_upgrade`):

```sh
perl mariadb-test-run.pl --parallel=4 \
  innodb.stats_zero_window innodb_gis.mdev-26123 \
  main.gis-malformed-wkb innodb_gis.rtree_malformed_wkb \
  innodb.innodb-stats-modified-counter \
  innodb.innodb_stats_reanalysis_counter \
  innodb.innodb_stats_reanalysis_upgrade \
  main.mysqldump-system main.mysqldump main.mysql_upgrade \
  funcs_1.is_columns_mysql innodb.innodb-system-table-view \
  innodb.innodb_stats_persistent \
  innodb.innodb_stats_rename_table_if_exists innodb.stat_tables \
  innodb_zip.page_size \
  type_mysql_timestamp.type_mysql_timestamp_stat_tables
```

## Limits

The intermediate LTS report checks patch application only. No intermediate
release was built or runtime-tested. Neither primary target has undergone a
full-server MTR run, sanitizer run, production workload benchmark, or
production packaging/deployment qualification in this maintenance pass.
No upstream submission or remote push was performed.

## Validation, 2026-10-19

The stats-zero-window topic was rebased onto main `327437ade1d` (13.2.0).
The conflict resolution preserves upstream's `IndexStats` type and
`stats_method` argument, along with the existing visibility fix and debug
sync test. Tested topic tip: `982715e710d`.

Integration was rebuilt from the five current topic tips. Its upgrade
regression and maintenance documentation had been dropped by the older
sync script and were restored. Tested integration tip: `dc6345dae2f`.
The old integration is retained as
`backup/integration-before-sync-repair-20261019`. The original September
maintenance tip is retained as
`backup/integration-with-maintenance-20260927`, keeping the recovery commits
and pinned source series reachable after garbage collection.

The 10.11 port remains based on `mariadb-10.11.19`; no server-code changes
were needed there. Tested maintenance tip: `0c344ad050c`. The reviewed HEAD
manifest and workflow instructions are shared between the maintained
branches, while September's source-series provenance is retained.

| Validation | Result |
| --- | --- |
| Stats topic: Debug minbuild and resolveip | Passed |
| Stats topic: visibility + three stats_method variants | 4 passed |
| Integration: Debug minbuild and upgrade/dump test plugins | Passed |
| Integration: five topics, upgrade, dump and stats_method variants | 12 passed |
| 10.11: installed sync script, release check and backport audit | Passed |
| 10.11: Debug build, five topics, upgrade and dump tests | 10 passed |
| Sync workflow tests in isolated Git repos, build/MTR stubbed | 10 passed |
| Bash syntax and ShellCheck | Passed |

The workflow tests cover preserving integration-only commits across
repeated rebuilds, dirty worktrees, failed fetches and merges, changed HEAD
patches, explicit release updates, test-gated publication, atomic leased
pushes, concurrent remote changes, and invalid publishing combinations.
The actual 10.11 script invocation completed with exit status zero:

```sh
~/mariadb-notes/sync-fork.sh --targets=10.11 --no-fetch --no-push --jobs=6
```

Durable logs and the pre-edit sync script are under
`/home/jsantala/mariadb-main/.claude/sync-repair-20261019/`.
The final script log directory is
`.claude/sync-logs/20261019-071056-124338/`.

GitHub was checked with `git ls-remote --heads origin`. `main` matches the
local upstream reference. Neither `integration` nor `maint/10.11` exists on
the remote; three older topic branches exist and the two WKB topics are
absent. No remote pushes were made in this pass. These are focused checks,
not a full MTR run or certification of intermediate MariaDB releases.
