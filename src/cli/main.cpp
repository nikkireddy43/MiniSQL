#include <chrono>
#include <iostream>
#include <sstream>
#include <string>

#include "buffer/BufferPool.h"
#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "executor/Executor.h"
#include "wal/WriteAheadLog.h"

using namespace minisql;

// Prints a SELECT result as a simple text table.
static void printResult(const ExecutionResult& result) {
    if (!result.columnNames.empty()) {
        for (const auto& col : result.columnNames) std::cout << col << "\t";
        std::cout << "\n";
        for (const auto& row : result.rows) {
            for (const auto& val : row) {
                switch (val.type) {
                    case ValueType::INT: std::cout << val.intVal; break;
                    case ValueType::FLOAT: std::cout << val.floatVal; break;
                    case ValueType::TEXT: std::cout << val.textVal; break;
                }
                std::cout << "\t";
            }
            std::cout << "\n";
        }
        std::cout << "(" << result.rows.size() << " row"
                   << (result.rows.size() == 1 ? "" : "s") << ")\n";
    }
    if (!result.message.empty()) {
        std::cout << result.message << "\n";
    }
}

// Builds a throwaway table, times point lookups BEFORE indexing (full
// table scan) vs AFTER indexing (B+Tree search), and prints the speedup.
// Statements are constructed directly as AST nodes rather than through
// the Lexer/Parser, so the timed portion measures only the storage/index
// lookup cost, not string parsing overhead.
static void runBenchmark(Executor& executor) {
    const std::string tableName = "bench_table";
    const int32_t n = 5000;

    std::cout << "Setting up benchmark table with " << n << " rows...\n";

    CreateTableStatement createStmt;
    createStmt.tableName = tableName;
    createStmt.columns = {{"id", ColumnType::INT}, {"val", ColumnType::TEXT}};
    executor.execute(&createStmt);

    for (int32_t i = 0; i < n; i++) {
        InsertStatement insertStmt;
        insertStmt.tableName = tableName;
        insertStmt.values = {
            Literal{Literal::Kind::INT, std::to_string(i)},
            Literal{Literal::Kind::STRING, "row" + std::to_string(i)},
        };
        executor.execute(&insertStmt);
    }
    std::cout << "Done. " << n << " rows inserted.\n";

    std::vector<int32_t> queryIds = {0, n / 4, n / 2, 3 * n / 4, n - 1};

    auto timeQueries = [&](const std::string& label) {
        auto start = std::chrono::steady_clock::now();
        for (int32_t qid : queryIds) {
            SelectStatement selStmt;
            selStmt.tableName = tableName;
            selStmt.selectAll = true;
            Condition cond;
            cond.column = "id";
            cond.op = TokenType::EQUAL;
            cond.value = Literal{Literal::Kind::INT, std::to_string(qid)};
            selStmt.whereClause = cond;
            executor.execute(&selStmt);
        }
        auto end = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << label << ": " << ms << " ms total for " << queryIds.size()
                   << " queries (" << (ms / queryIds.size()) << " ms/query avg)\n";
        return ms;
    };

    std::cout << "\n--- BEFORE indexing (full table scan) ---\n";
    double fullScanMs = timeQueries("Full scan");

    CreateIndexStatement idxStmt;
    idxStmt.indexName = "idx_bench_id";
    idxStmt.tableName = tableName;
    idxStmt.columnName = "id";
    executor.execute(&idxStmt);
    std::cout << "\nIndex created on '" << tableName << "(id)'.\n";

    std::cout << "\n--- AFTER indexing (B+Tree lookup) ---\n";
    double indexedMs = timeQueries("Indexed");

    std::cout << "\nSpeedup: " << (fullScanMs / indexedMs) << "x faster with the index.\n";

    DropTableStatement dropStmt;
    dropStmt.tableName = tableName;
    executor.execute(&dropStmt);
    std::cout << "(benchmark table dropped)\n\n";
}

int main() {
    std::string catalogPath = "minisql_catalog.db";
    std::string dataPath = "minisql_data.db";
    std::string walPath = "minisql.wal";

    CatalogManager catalog(catalogPath);
    DiskManager dataDisk(dataPath);

    // Replay any log entries left over from a prior run that crashed
    // before its real page writes completed - MUST happen before the
    // buffer pool starts caching pages, so recovery writes land on the
    // actual file first.
    WriteAheadLog::recover(walPath, dataDisk);

    WriteAheadLog wal(walPath);
    BufferPool bufferPool(dataDisk, /*poolSize=*/64);
    Executor executor(catalog, bufferPool, wal, dataPath, catalogPath);

    std::cout << "MiniSQL v1\n";
    std::cout << "Type SQL statements ending in ';' (including BEGIN/COMMIT/"
                 "ROLLBACK), BENCHMARK to run the indexed-vs-scan benchmark, "
                 "or EXIT to quit.\n";

    std::string line;
    std::string buffer;

    while (true) {
        std::cout << "MiniSQL> ";
        if (!std::getline(std::cin, line)) break;  // EOF (e.g. Ctrl+D)

        std::string trimmed = line;
        // crude trim
        while (!trimmed.empty() && std::isspace(static_cast<unsigned char>(trimmed.back()))) {
            trimmed.pop_back();
        }
        if (trimmed == "EXIT" || trimmed == "exit") break;
        if (trimmed.empty()) continue;

        if (trimmed == "BENCHMARK" || trimmed == "benchmark") {
            try {
                runBenchmark(executor);
            } catch (const std::exception& e) {
                std::cout << "Benchmark error: " << e.what() << "\n";
            }
            buffer.clear();
            continue;
        }

        buffer += line + " ";
        if (buffer.find(';') == std::string::npos) {
            continue;  // keep accumulating until we see a semicolon
        }

        try {
            Lexer lexer(buffer);
            Parser parser(lexer.tokenize());
            auto stmt = parser.parseStatement();
            ExecutionResult result = executor.execute(stmt.get());
            printResult(result);
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
        }

        buffer.clear();
    }

    executor.checkpoint();  // flush everything for real and clear the WAL log
    std::cout << "Goodbye.\n";
    return 0;
}

