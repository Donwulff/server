#!/usr/bin/env bash
#
# Run the MTR test suites most likely to notice MDEV-24303 regressions.
#
# Usage: mtr.sh [extra mtr args...]
#
# Runs all stats-related tests (explicit list plus a wildcard match).
# Pass extra mtr args after the script name, e.g.:
#   mtr.sh --mem --parallel=8
#   mtr.sh --record   # to re-record test results after intentional change

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
: "${BUILD_DIR:=$SCRIPT_DIR/../build}"
BUILD_DIR=$(cd "$BUILD_DIR" && pwd)
MTR_DIR="$BUILD_DIR/mysql-test"

[ -x "$MTR_DIR/mariadb-test-run.pl" ] || {
  echo "mariadb-test-run.pl not found under $MTR_DIR" >&2
  exit 1
}

cd "$MTR_DIR"

TESTS=(
  # Whole suite
  --suite=innodb_stats
  # Specific tests that exercise dict_stats_update_* directly
  main.stat_tables_innodb
  main.mysql_upgrade
  # Wildcard catches everything with "stats" in the name
  # (innodb.*stats, main.*stats, etc.)
  --do-test=stats
)

exec ./mariadb-test-run.pl \
  --force \
  --parallel=${MTR_PARALLEL:-4} \
  --retry=0 \
  "${TESTS[@]}" \
  "$@"
