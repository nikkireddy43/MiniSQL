#include <iostream>
#include <sstream>
#include <string>

#include "lexer/Lexer.h"
#include "parser/Parser.h"
#include "executor/Executor.h"

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

int main() {
    CatalogManager catalog("minisql_catalog.db");
    DiskManager dataDisk("minisql_data.db");
    Executor executor(catalog, dataDisk);

    std::cout << "MiniSQL v1\n";
    std::cout << "Type SQL statements ending in ';', or EXIT to quit.\n";

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

    std::cout << "Goodbye.\n";
    return 0;
}
