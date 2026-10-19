# MDEV-24303 benchmark harness

Three helpers that live under `bench/` at the repo/worktree root:

- `bench.sh` — spawns a throwaway mariadbd, runs sysbench, writes
  results to `bench/results/<label>-<timestamp>/`.
- `compare.sh` — diffs two `bench.sh` result directories into a
  Markdown table.
- `mtr.sh` — runs the stats-related MTR tests in the in-tree
  `mysql-test/` directory.

## Prerequisites

Install once per machine:

```
sudo apt install -y sysbench
```

The in-tree binaries must already be built:

```
cmake --build build --target mariadbd -j
```

All three scripts default `BUILD_DIR` to `<repo-root>/build`. For a
non-standard build tree, set `BUILD_DIR=/path/to/build` or point
directly at specific binaries via `BINARY`, `CLIENT`, `INSTALL_DB`.

## Before/after run

```bash
# 1. Baseline: rebuild on main (or pre-MDEV-24303 commit), label it.
BINARY=/path/to/baseline/build/sql/mariadbd \
CLIENT=/path/to/baseline/build/client/mariadb \
INSTALL_DB=/path/to/baseline/build/scripts/mariadb-install-db \
  ./bench/bench.sh baseline

# 2. Patched: default env uses the current build.
./bench/bench.sh patched

# 3. Diff the two result directories.
./bench/compare.sh bench/results/baseline-<ts> bench/results/patched-<ts>
```

Results per run:

- `env.txt` — binary sha, host, git head, CPU, sysbench version
- `server.cnf`, `server.log` — the mariadbd instance config/log
- `raw_<test>_t<n>_iter<i>.txt` — one sysbench stdout per run
- `summary.csv` — per-iteration TPS/QPS/latency
- `medians.csv` — median per (test, threads) combination
- `tablestats_after.txt` — I_S.INNODB_SYS_TABLESTATS after workload
- `innodb_table_stats_after.txt` — `reanalysis_counter` values after run

## What the defaults measure

- Buffer pool 4 GiB (override `BP_SIZE=...`) — enough to keep 1M × 4
  tables in memory so we measure write-path CPU, not I/O.
- `innodb_flush_log_at_trx_commit=0`, doublewrite off — benchmarking
  the stats counter path, not durability.
- 4 tables × 1,000,000 rows (override `TABLES`, `TABLE_SIZE`).
- 60s measurement runs × 3 iterations (override `DURATION`,
  `ITERATIONS`), plus a 10s warmup per (test, threads).
- Thread sweep `1 4 16 64` (override `THREADS="1 2 4 ..."`).
- Tests `oltp_update_index oltp_write_only` (override `TESTS=...`).

Both tests hammer `dict_stats_update_if_needed()` because every UPDATE
or INSERT/DELETE calls it. `oltp_update_index` isolates the atomic
fetch_sub cost (index-key changes only); `oltp_write_only` adds
ordering-preserving UPDATEs and delete marks to cover the no-ord-change
path that now goes through `stat_modified_counter.fetch_add(1)` without
touching the countdown.

## Reading the results

The patch claim: hot path should be within noise, contention curve
should flatten at high thread counts (no more lost-update races on the
non-atomic counter, and no `n_rows / 10` reset write from the slow
path on every DML).

Expected shape of `compare.sh` output:

- At `threads=1`: TPS deltas within ±1-2% (noise).
- At `threads=64`: patched should match or exceed baseline; any
  regression > 2% is worth investigating with `perf` on the
  `dict_stats_update_if_needed` hot path.
- `reanalysis_counter` column in `innodb_table_stats_after.txt` should
  be ≤ the recalc threshold (roughly `n_rows / 10`) for each sbtest
  table. A value stuck at 0 means auto-recalc is triggering too often;
  a value that never decreases means events aren't being counted.

## MTR regression

```
./bench/mtr.sh                       # default parallelism = 4
MTR_PARALLEL=16 ./bench/mtr.sh --mem # use tmpfs, 16-way parallel
```

What it covers:

- `innodb_stats` suite in full
- Any test matching `stats` (wildcard) — catches
  `innodb.innodb_stat_tables`, `main.stat_tables`, etc.
- `main.stat_tables_innodb` explicit
- `main.mysql_upgrade` — verifies the `reanalysis_counter` column
  backfill path works on old datadirs.

Failures to expect:

- `innodb_table_stats` structural tests — the added column will show
  up in any test that `SHOW CREATE TABLE mysql.innodb_table_stats` or
  does a `DESCRIBE`. Update the `.result` files if the diff is only
  the new column.
- Any test that compares `MODIFIED_COUNTER` values after DML — the
  semantics of the cumulative counter changed (no reset on recalc).
  Adjust expectations.
- `mysql_upgrade` — may need `.result` updates for the new
  `ALTER TABLE ... ADD COLUMN reanalysis_counter` line in the upgrade
  output.

Use `./bench/mtr.sh --record <single-test>` to re-record a `.result`
file after verifying the new output is correct.

## Running on a different machine

Everything is self-contained under `bench/`. Copy the directory plus
the built binaries to the new machine, install sysbench, and run as
above. The `env.txt` captured in each result directory pins the exact
binary sha and host so runs from different machines can be compared
without ambiguity.
