# MiniSQL

A lightweight relational database engine built from scratch in C++17 —
custom SQL parser, disk-backed storage, and a working execution engine
with CRUD support, all built and tested layer by layer.

## Status

✅ **v1 complete** — Lexer, Parser, Storage Engine, Catalog Manager,
Execution Engine, and CLI all working end-to-end. 60/60 tests passing.

🚧 **v2 next** — B+Tree indexing, buffer pool, transactions, WAL-based
crash recovery.

## Example session

```
$ ./minisql
MiniSQL v1
Type SQL statements ending in ';', or EXIT to quit.
MiniSQL> CREATE TABLE student(id INT, name TEXT, cg FLOAT);
Table 'student' created.
MiniSQL> INSERT INTO student VALUES(1, 'Nikki', 8.7);
1 row inserted.
MiniSQL> SELECT * FROM student;
id      name    cg
1       Nikki   8.7
(1 row)
1 row(s) returned.
```

## Roadmap

- [x] **v1 — Working Database**: SQL Parser → Storage Engine → Catalog
      Manager → Execution Engine → CLI
- [ ] **v2 — Fast & Durable**: B+Tree Index → Buffer Pool → Transactions
      → Write-Ahead Logging → Benchmarks
- [ ] **v3 — Query Smarts**: Cost-Based Optimizer → Joins → Aggregations
      → Sorting

## Architecture

```
SQL Query -> Lexer -> Parser -> AST -> Execution Engine
    -> Catalog Manager (schema/metadata)
    -> Page / DiskManager (disk-backed row storage)
```

Data model: every table's rows live as serialized `Record`s packed into
fixed-size 4096-byte `Page`s in a shared data file. Table schemas are
tracked by the `CatalogManager` and persisted the same way - as records
in their own page, reusing the same storage engine as user data.

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

```bash
./build/minisql
```

## Tech Stack

C++17, CMake, GoogleTest

