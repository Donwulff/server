#!/usr/bin/env bash
#
# MDEV-24303 benchmark harness.
#
# Spawns a throwaway mariadbd instance from an in-tree build and drives it
# with sysbench workloads that exercise the stats-counter hot path
# (dict_stats_update_if_needed).  Writes per-run latency/TPS/QPS numbers
# plus a median summary to $OUTDIR/<label>-<timestamp>/.
#
# Usage:
#   bench.sh [label]                # run against default $BINARY
#   BINARY=/path/to/other/mariadbd bench.sh baseline
#
# Environment overrides (all optional):
#   BINARY       mariadbd executable (default: ../sql/mariadbd)
#   CLIENT       mariadb client      (default: ../client/mariadb)
#   INSTALL_DB   mariadb-install-db  (default: ../scripts/mariadb-install-db)
#   LABEL        tag for this run    (default: $1 or "patched")
#   DATADIR      scratch datadir     (default: /tmp/mdev24303-<LABEL>)
#   SOCKET       unix socket path    (default: $DATADIR/mysql.sock)
#   OUTDIR       results root        (default: ./results/<label>-<ts>)
#   BP_SIZE      innodb buffer pool  (default: 4G)
#   TABLE_SIZE   rows per table      (default: 1000000)
#   TABLES       sysbench tables     (default: 4)
#   DURATION     run seconds         (default: 60)
#   WARMUP       warmup seconds      (default: 10)
#   ITERATIONS   measured runs       (default: 3)
#   THREADS      thread counts list  (default: "1 4 16 64")
#   TESTS        sysbench scripts    (default: "oltp_update_index oltp_write_only")
#   STATS_MODE   persistent|transient|both (default: persistent)
#   SKIP_PREPARE 1 to reuse existing DATADIR/tables (default: 0)
#
# Prerequisites:
#   - sysbench >= 1.0 (apt: sysbench; it must be able to locate
#     oltp_update_index.lua — on Debian/Ubuntu that's /usr/share/sysbench).
#   - A built tree (sql/mariadbd, client/mariadb, scripts/mariadb-install-db).
#
# Before/after comparison: run this script twice — once with $BINARY
# pointing at a baseline build, once with $BINARY pointing at the
# MDEV-24303 build — and diff the resulting summary.csv files.

set -euo pipefail

# --- Locate this script and the build tree ---
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
# Default BUILD_DIR assumes a sibling build/ next to bench/ (i.e., the
# worktree has layout {bench/, build/, sql/, ...}). Override via env.
: "${BUILD_DIR:=$SCRIPT_DIR/../build}"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)

# --- Defaults (override via env) ---
: "${BINARY:=$BUILD_DIR/sql/mariadbd}"
: "${CLIENT:=$BUILD_DIR/client/mariadb}"
: "${INSTALL_DB:=$BUILD_DIR/scripts/mariadb-install-db}"
: "${LABEL:=${1:-patched}}"
: "${DATADIR:=/tmp/mdev24303-$LABEL}"
: "${SOCKET:=$DATADIR/mysql.sock}"
: "${PIDFILE:=$DATADIR/mariadbd.pid}"
: "${PORT:=0}" # 0 = socket only
: "${BP_SIZE:=4G}"
: "${TABLE_SIZE:=1000000}"
: "${TABLES:=4}"
: "${DURATION:=60}"
: "${WARMUP:=10}"
: "${ITERATIONS:=3}"
: "${THREADS:=1 4 16 64}"
: "${TESTS:=oltp_update_index oltp_write_only}"
: "${STATS_MODE:=persistent}"
: "${SKIP_PREPARE:=0}"

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
: "${OUTDIR:=$SCRIPT_DIR/results/${LABEL}-${TIMESTAMP}}"
mkdir -p "$OUTDIR"

log() { printf '[bench %s] %s\n' "$(date +%H:%M:%S)" "$*" | tee -a "$OUTDIR/run.log"; }

# --- Prerequisite checks ---
for tool in sysbench awk grep sed; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing: $tool" >&2; exit 1; }
done
for f in "$BINARY" "$CLIENT" "$INSTALL_DB"; do
  [ -x "$f" ] || { echo "not executable: $f" >&2; exit 1; }
done

# --- Record build provenance ---
{
  echo "label=$LABEL"
  echo "binary=$BINARY"
  echo "binary_sha=$(sha256sum "$BINARY" | awk '{print $1}')"
  echo "host=$(uname -a)"
  echo "cpu=$(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')"
  echo "cpu_cores=$(nproc)"
  echo "git_head=$(git -C "$BUILD_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "git_status=$(git -C "$BUILD_DIR" status --porcelain 2>/dev/null | head -5)"
  echo "sysbench=$(sysbench --version)"
  echo "date=$(date -u +%FT%TZ)"
  echo "table_size=$TABLE_SIZE"
  echo "tables=$TABLES"
  echo "duration=$DURATION"
  echo "warmup=$WARMUP"
  echo "iterations=$ITERATIONS"
  echo "threads=$THREADS"
  echo "tests=$TESTS"
  echo "bp_size=$BP_SIZE"
  echo "stats_mode=$STATS_MODE"
} > "$OUTDIR/env.txt"

# --- Server lifecycle ---
CNF="$OUTDIR/server.cnf"
cat > "$CNF" <<EOF
[mysqld]
datadir         = $DATADIR
socket          = $SOCKET
pid-file        = $PIDFILE
log-error       = $OUTDIR/server.log
port            = $PORT
skip-networking = ${SKIP_NETWORK:-ON}
skip-name-resolve
# Tuned for write-path measurement, not durability:
innodb_buffer_pool_size        = $BP_SIZE
innodb_log_file_size           = 1G
innodb_flush_log_at_trx_commit = 0
innodb_flush_method            = O_DIRECT
innodb_doublewrite             = 0
innodb_stats_persistent        = $( [ "$STATS_MODE" = transient ] && echo OFF || echo ON )
innodb_stats_auto_recalc       = ON
performance_schema             = OFF
EOF

wait_for_server() {
  local tries=60
  while [ $tries -gt 0 ]; do
    if "$CLIENT" --socket="$SOCKET" -u root -e 'SELECT 1' >/dev/null 2>&1; then
      return 0
    fi
    sleep 1
    tries=$((tries - 1))
  done
  echo "server did not come up" >&2
  tail -50 "$OUTDIR/server.log" >&2 || true
  return 1
}

stop_server() {
  if [ -f "$PIDFILE" ]; then
    local pid
    pid=$(cat "$PIDFILE" 2>/dev/null || true)
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
      log "stopping mariadbd pid=$pid"
      "$CLIENT" --socket="$SOCKET" -u root -e 'SHUTDOWN' 2>/dev/null || kill "$pid"
      # Wait for shutdown
      local tries=30
      while [ $tries -gt 0 ] && kill -0 "$pid" 2>/dev/null; do
        sleep 1
        tries=$((tries - 1))
      done
    fi
  fi
}

start_server() {
  log "starting mariadbd, datadir=$DATADIR"
  # No --daemonize in MariaDB 13.x; background the process and let
  # mariadbd write its own pid-file via the my.cnf directive.
  nohup "$BINARY" --defaults-file="$CNF" \
    >> "$OUTDIR/server.log" 2>&1 &
  SERVER_PID=$!
  disown || true
  wait_for_server
  log "server ready on $SOCKET (pid=$SERVER_PID)"
}

bootstrap_datadir() {
  if [ "$SKIP_PREPARE" = 1 ] && [ -d "$DATADIR/mysql" ]; then
    log "skipping bootstrap (SKIP_PREPARE=1, reusing $DATADIR)"
    return 0
  fi
  rm -rf "$DATADIR"
  mkdir -p "$DATADIR"
  log "bootstrapping datadir $DATADIR"
  "$INSTALL_DB" \
    --builddir="$BUILD_DIR" \
    --srcdir="$(cd "$BUILD_DIR/.." && pwd)" \
    --datadir="$DATADIR" \
    --auth-root-authentication-method=normal \
    --skip-test-db \
    --user="$(id -un)" > "$OUTDIR/install-db.log" 2>&1
}

trap stop_server EXIT INT TERM

# --- Boot sequence ---
bootstrap_datadir
start_server

# Create sysbench database, allow root from localhost without password.
"$CLIENT" --socket="$SOCKET" -u root -e "
CREATE DATABASE IF NOT EXISTS sbtest;
CREATE USER IF NOT EXISTS sbuser@'localhost' IDENTIFIED BY 'sbuser';
GRANT ALL ON sbtest.* TO sbuser@'localhost';
FLUSH PRIVILEGES;"

SB_ARGS=(
  --db-driver=mysql
  --mysql-socket="$SOCKET"
  --mysql-user=sbuser
  --mysql-password=sbuser
  --mysql-db=sbtest
  --tables="$TABLES"
  --table-size="$TABLE_SIZE"
  --report-interval=10
  --histogram=on
  --rand-type=uniform
)

# --- sysbench prepare (once per datadir) ---
if [ "$SKIP_PREPARE" != 1 ]; then
  log "sysbench prepare ($TABLES tables x $TABLE_SIZE rows)"
  # Use the first test script for prepare; schema is shared.
  FIRST_TEST=$(echo "$TESTS" | awk '{print $1}')
  sysbench "$FIRST_TEST" "${SB_ARGS[@]}" prepare \
    > "$OUTDIR/prepare.log" 2>&1
fi

# --- Run matrix ---
SUMMARY="$OUTDIR/summary.csv"
echo "label,test,threads,iter,tps,qps,lat_avg_ms,lat_p95_ms,errors,reconnects" > "$SUMMARY"

parse_sb() {
  # $1 = raw sysbench output file
  local f=$1
  local tps qps lat_avg lat_p95 errs rec
  tps=$(awk '/transactions:/ {gsub(/[()]/,""); print $3; exit}' "$f")
  qps=$(awk '/queries:/      {gsub(/[()]/,""); print $3; exit}' "$f")
  lat_avg=$(awk '/avg:/ && !seen {print $2; seen=1}' "$f")
  lat_p95=$(awk '/95th percentile:/ {print $3; exit}' "$f")
  errs=$(awk '/errors:/ {print $3; exit}' "$f")
  rec=$(awk '/reconnects:/ {print $3; exit}' "$f")
  printf '%s,%s,%s,%s,%s,%s\n' \
    "${tps:-0}" "${qps:-0}" "${lat_avg:-0}" "${lat_p95:-0}" \
    "${errs:-0}" "${rec:-0}"
}

for test in $TESTS; do
  for threads in $THREADS; do
    log "warmup: $test threads=$threads"
    sysbench "$test" "${SB_ARGS[@]}" --threads="$threads" \
      --time="$WARMUP" run \
      > "$OUTDIR/raw_${test}_t${threads}_warmup.txt" 2>&1 || true

    for iter in $(seq 1 "$ITERATIONS"); do
      log "run: $test threads=$threads iter=$iter"
      raw="$OUTDIR/raw_${test}_t${threads}_iter${iter}.txt"
      sysbench "$test" "${SB_ARGS[@]}" --threads="$threads" \
        --time="$DURATION" run > "$raw" 2>&1
      line=$(parse_sb "$raw")
      echo "$LABEL,$test,$threads,$iter,$line" >> "$SUMMARY"
    done
  done
done

# --- Collect counter metrics from I_S for a sanity check ---
log "collecting INNODB_SYS_TABLESTATS snapshot"
"$CLIENT" --socket="$SOCKET" -u root -e "
  SELECT name, modified_counter, num_rows
  FROM information_schema.innodb_sys_tablestats
  WHERE name LIKE 'sbtest/%' ORDER BY name;" \
  > "$OUTDIR/tablestats_after.txt" 2>&1 || true

"$CLIENT" --socket="$SOCKET" -u root -e "
  SELECT database_name, table_name, n_rows, reanalysis_counter
  FROM mysql.innodb_table_stats
  WHERE database_name='sbtest' ORDER BY table_name;" \
  > "$OUTDIR/innodb_table_stats_after.txt" 2>&1 || true

# --- Median summary ---
log "computing medians"
awk -F, 'NR==1 {next}
  {
    k=$2 FS $3; tps[k]=tps[k]" "$5; qps[k]=qps[k]" "$6;
    lat[k]=lat[k]" "$7; p95[k]=p95[k]" "$8;
  }
  END {
    print "test,threads,median_tps,median_qps,median_lat_avg_ms,median_lat_p95_ms";
    for (k in tps) {
      print k "," med(tps[k]) "," med(qps[k]) "," med(lat[k]) "," med(p95[k]);
    }
  }
  function med(s,    a,n,i) {
    n=split(s,a," "); asort(a);
    if (n%2) return a[(n+1)/2]; else return (a[n/2]+a[n/2+1])/2;
  }' "$SUMMARY" | sort > "$OUTDIR/medians.csv"

log "done. results in $OUTDIR"
echo
cat "$OUTDIR/medians.csv"
