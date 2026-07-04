# MiniSQL

A lightweight relational database engine built from scratch in C++17 —
custom SQL parser, disk-backed storage, B+Tree indexing, buffer pool,
and ACID-compliant transactions with crash recovery.

## Status

🚧 **v1 in progress** — currently implementing the SQL Lexer.

## Roadmap

- [ ] **v1 — Working Database**: SQL Parser → Storage Engine → Catalog
      Manager → Execution Engine → CLI
- [ ] **v2 — Fast & Durable**: B+Tree Index → Buffer Pool → Transactions
      → Write-Ahead Logging → Benchmarks
- [ ] **v3 — Query Smarts**: Cost-Based Optimizer → Joins → Aggregations
      → Sorting

## Building

Requires CMake 3.16+ and a C++17 compiler. GoogleTest is fetched
automatically — no manual install needed.

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

## Architecture

```
SQL Query -> Lexer -> Parser -> AST -> Query Planner -> Optimizer
    -> Execution Engine -> Buffer Manager / B+Tree Index
    -> Storage Engine (Pages) -> Disk Files (.db)
```

## Tech Stack

C++17, CMake, GoogleTest, Docker (later)
