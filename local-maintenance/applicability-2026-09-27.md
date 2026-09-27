# Local patch applicability

Each topic is checked independently against the unpatched target.
Clean three-way merges still require compilation and MTR tests.

| Target (commit) | Topic | Direct apply | Three-way merge |
| --- | --- | --- | --- |
| mariadb-10.11.19 (93e051860a9c) | stats-zero-window | no | conflicts |
| mariadb-10.11.19 (93e051860a9c) | mdev-26123 | clean | clean |
| mariadb-10.11.19 (93e051860a9c) | spatial-interiorring-wkb | clean | clean |
| mariadb-10.11.19 (93e051860a9c) | mdev-38372-defense | clean | clean |
| mariadb-10.11.19 (93e051860a9c) | mdev-24303 | no | conflicts |
| mariadb-11.4.13 (170b1d70737b) | stats-zero-window | clean | clean |
| mariadb-11.4.13 (170b1d70737b) | mdev-26123 | clean | clean |
| mariadb-11.4.13 (170b1d70737b) | spatial-interiorring-wkb | clean | clean |
| mariadb-11.4.13 (170b1d70737b) | mdev-38372-defense | clean | clean |
| mariadb-11.4.13 (170b1d70737b) | mdev-24303 | no | conflicts |
| mariadb-11.8.9 (bf9193a939f5) | stats-zero-window | clean | clean |
| mariadb-11.8.9 (bf9193a939f5) | mdev-26123 | clean | clean |
| mariadb-11.8.9 (bf9193a939f5) | spatial-interiorring-wkb | clean | clean |
| mariadb-11.8.9 (bf9193a939f5) | mdev-38372-defense | clean | clean |
| mariadb-11.8.9 (bf9193a939f5) | mdev-24303 | no | conflicts |
| mariadb-12.3.3 (83e909fc2a0d) | stats-zero-window | clean | clean |
| mariadb-12.3.3 (83e909fc2a0d) | mdev-26123 | clean | clean |
| mariadb-12.3.3 (83e909fc2a0d) | spatial-interiorring-wkb | clean | clean |
| mariadb-12.3.3 (83e909fc2a0d) | mdev-38372-defense | clean | clean |
| mariadb-12.3.3 (83e909fc2a0d) | mdev-24303 | no | conflicts |
| 45717ef4c736a6a92e0acf49399ce677bfcaea89 (45717ef4c736) | stats-zero-window | clean | clean |
| 45717ef4c736a6a92e0acf49399ce677bfcaea89 (45717ef4c736) | mdev-26123 | clean | clean |
| 45717ef4c736a6a92e0acf49399ce677bfcaea89 (45717ef4c736) | spatial-interiorring-wkb | clean | clean |
| 45717ef4c736a6a92e0acf49399ce677bfcaea89 (45717ef4c736) | mdev-38372-defense | clean | clean |
| 45717ef4c736a6a92e0acf49399ce677bfcaea89 (45717ef4c736) | mdev-24303 | clean | clean |

Conflicting paths:

- mariadb-10.11.19, stats-zero-window: storage/innobase/dict/dict0stats.cc
- mariadb-10.11.19, mdev-24303: mysql-test/main/mysqldump-system.result, mysql-test/suite/funcs_1/r/is_columns_mysql.result, storage/innobase/dict/dict0stats.cc, storage/innobase/dict/dict0stats_bg.cc
- mariadb-11.4.13, mdev-24303: mysql-test/main/mysqldump-system.result, mysql-test/suite/funcs_1/r/is_columns_mysql.result, storage/innobase/dict/dict0stats.cc, storage/innobase/dict/dict0stats_bg.cc
- mariadb-11.8.9, mdev-24303: mysql-test/suite/funcs_1/r/is_columns_mysql.result, storage/innobase/dict/dict0stats_bg.cc
- mariadb-12.3.3, mdev-24303: mysql-test/suite/funcs_1/r/is_columns_mysql.result, storage/innobase/dict/dict0stats_bg.cc
