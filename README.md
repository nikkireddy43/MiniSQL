# MiniSQL

A relational database engine built from scratch in C++17 — custom SQL
parser, disk-backed storage with B+Tree indexing, an LRU buffer pool,
snapshot-based transactions, write-ahead-log crash recovery, and a
query engine supporting joins, aggregations, and sorting. Built and
tested layer by layer, with 121 passing tests.

## Status

✅ **v1 complete** — Lexer, Parser, Storage Engine, Catalog Manager,
Execution Engine, and CLI, all working end-to-end.

✅ **v2 complete** — B+Tree indexing, LRU buffer pool, snapshot-based
transactions (BEGIN/COMMIT/ROLLBACK), and write-ahead logging with
crash recovery — all integrated into the execution path, not just
built in isolation.

✅ **v3 complete** — INNER/LEFT joins with a cost-based nested-loop-vs-
hash-join decision, aggregate functions with GROUP BY, ORDER BY, and
`.sql` script file execution.

**121/121 tests passing.**

## Example session

```
$ ./minisql
MiniSQL v1
Type SQL statements ending in ';' (including BEGIN/COMMIT/ROLLBACK),
BENCHMARK to run the indexed-vs-scan benchmark, or EXIT to quit.
MiniSQL> CREATE TABLE student(id INT, name TEXT, cg FLOAT);
Table 'student' created.
MiniSQL> INSERT INTO student VALUES(1, 'Nikki', 8.7);
1 row inserted.
MiniSQL> CREATE TABLE course(id INT, studentId INT, code TEXT);
Table 'course' created.
MiniSQL> INSERT INTO course VALUES(1, 1, 'CS101');
1 row inserted.
MiniSQL> SELECT student.name, course.code FROM student
          JOIN course ON student.id = course.studentId;
student.name    course.code
Nikki           CS101
(1 row)
1 row(s) returned (used nested loop join).
MiniSQL> SELECT COUNT(*), AVG(cg) FROM student;
COUNT(*)        AVG(cg)
1               8.7
(1 row)
1 row(s) returned.
MiniSQL> BEGIN;
Transaction started.
MiniSQL> INSERT INTO student VALUES(2, 'Ghost', 0.0);
1 row inserted.
MiniSQL> ROLLBACK;
Transaction rolled back.
MiniSQL> SELECT * FROM student ORDER BY cg DESC;
id      name    cg
1       Nikki   8.7
(1 row)
1 row(s) returned.
```

The rolled-back insert never happened as far as the data is concerned,
even though it executed and returned success at the time.

Running `BENCHMARK` builds a 5,000-row table and times point lookups
before and after indexing - on a typical run, indexed lookups are
50-70x faster than a full table scan.

Running `./minisql script.sql` executes every statement in a file in
order and exits, instead of starting the interactive REPL.

## Roadmap

- [x] **v1 — Working Database**: SQL Parser → Storage Engine → Catalog
      Manager → Execution Engine → CLI
- [x] **v2 — Fast & Durable**: B+Tree Index → Buffer Pool → Transactions
      → Write-Ahead Logging → Benchmarks
- [x] **v3 — Query Smarts**: Cost-Based Join Strategy → Joins →
      Aggregations → Sorting

## Architecture

```
SQL Query -> Lexer -> Parser -> AST -> Execution Engine
    -> Join Engine (nested loop / hash join, cost-based choice)
    -> Aggregation Engine (COUNT/SUM/AVG/MIN/MAX, GROUP BY, ORDER BY)
    -> B+Tree Index (point lookups on indexed columns)
    -> Buffer Pool (LRU-cached page I/O, pin counting, dirty tracking)
    -> Write-Ahead Log (redo logging + crash recovery)
    -> Catalog Manager (schema/metadata, persisted the same way as data)
    -> Page / DiskManager (disk-backed row storage)
```

**Data model.** Every table's rows live as serialized `Record`s packed
into fixed-size 4096-byte `Page`s in a shared data file. Table schemas
are tracked by the `CatalogManager` and persisted the same way - as
records in their own page, reusing the same storage engine as user
data.

**Reads and writes go through the Buffer Pool**, not straight to disk -
pages are cached in memory, with LRU eviction reclaiming the least
recently used *unpinned* frame when the pool is full.

**Durability comes from the Write-Ahead Log**, not eager flushing: every
modified page is logged (a small, sequential append) before the Buffer
Pool is allowed to defer the real disk write. On startup, `recover()`
replays the log onto the data file before anything else touches it,
so a crash between "logged" and "actually written" is never lost. A
`checkpoint()` flushes everything for real and clears the log once it's
no longer needed.

**Transactions are snapshot-based**: `BEGIN` copies the data and catalog
files aside; `ROLLBACK` restores them and resets every in-memory
structure that would otherwise still reflect the undone changes (buffer
pool cache, catalog metadata, in-memory indexes); `COMMIT` just discards
the snapshot, since every statement is already durable via the WAL.

**Indexes are an in-memory B+Tree** per indexed column, kept in sync
incrementally on `INSERT` and rebuilt from a full scan after `UPDATE`/
`DELETE` (which rewrite row locations). `SELECT ... WHERE col = X` uses
the index automatically when one exists for an equality condition.

**Joins pick their own strategy.** `student JOIN course ON student.id =
course.studentId` runs as a nested loop by default; once both sides of
an INNER JOIN exceed 50 rows, the engine switches to a hash join
(building a hash table on the smaller side) instead - a basic
cardinality-based cost decision, reported in the result message so you
can see which one ran. LEFT JOIN always uses nested loop, to keep
unmatched-row handling (NULL-filling) simple.

**Aggregates and GROUP BY** compute over an in-memory scan of the
(optionally WHERE-filtered) rows; groups are collected into a
`std::map` keyed by a string form of the group column's value, giving
deterministic, sorted-by-key output order.

## Known limitations (deliberate scope cuts, not oversights)

- `UPDATE`/`DELETE` rewrite a table's entire page set rather than
  editing pages in place.
- The B+Tree supports point lookups only (no range scans, no leaf
  sibling chain) and simple delete (no merge/redistribution on
  underflow).
- Indexes are in-memory only, dropped (not rebuilt) on transaction
  rollback - recreate with `CREATE INDEX` afterward if needed.
- The WAL logs page writes for redo/crash-recovery only; it does not
  implement undo logging - transaction rollback is handled separately
  via the snapshot mechanism above.
- No concurrency control - this is a single-threaded, single-connection
  engine.
- Aggregates combined with JOIN in the same query are not supported
  (throws a clear error rather than producing silently wrong results).
- Mixing plain columns with aggregates in a SELECT list is not
  supported, other than the GROUP BY column, which is included
  automatically.
- Hash join is only used for INNER JOIN; LEFT JOIN always uses nested
  loop.
- `.sql` script files (and the REPL) split statements on `;` textually -
  a `;` inside a quoted string literal would be misread as a statement
  boundary.

## Getting the code

```bash
git clone https://github.com/nikkireddy43/MiniSQL.git
cd MiniSQL
```

## Building

Requires CMake 3.16+ and a C++17 compiler. GoogleTest is fetched
automatically - no manual install needed.

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

## Running tests

```bash
cd build
ctest --output-on-failure
```

## Running the CLI

Interactive REPL:
```bash
./build/minisql
```

Run a `.sql` script file and exit:
```bash
./build/minisql path/to/script.sql
```

## Tech Stack

C++17, CMake, GoogleTest



