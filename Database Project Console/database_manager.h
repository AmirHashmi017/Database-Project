// database_manager.h (updated)
#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include "catalog.h"
#include "bptree.h"
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <shlobj.h>  // For SHGetKnownFolderPath
#pragma comment(lib, "shell32.lib")  // Link with shell32.lib
#ifdef _WIN32
#include <windows.h> // For Windows systems
#else
#include <unistd.h> // For Unix systems
#include <limits.h> // For PATH_MAX
#endif

// Define a generic record type that can hold different data types
using FieldValue = std::variant<int, float, std::string, bool>;
using Record = std::map<std::string, FieldValue>;

// Comparison operators for FieldValue
inline bool operator==(const FieldValue& lhs, const FieldValue& rhs) {
    if (std::holds_alternative<int>(lhs) && std::holds_alternative<int>(rhs))
        return std::get<int>(lhs) == std::get<int>(rhs);
    else if (std::holds_alternative<float>(lhs) && std::holds_alternative<float>(rhs))
        return std::get<float>(lhs) == std::get<float>(rhs);
    else if (std::holds_alternative<std::string>(lhs) && std::holds_alternative<std::string>(rhs))
        return std::get<std::string>(lhs) == std::get<std::string>(rhs);
    else if (std::holds_alternative<bool>(lhs) && std::holds_alternative<bool>(rhs))
        return std::get<bool>(lhs) == std::get<bool>(rhs);
    return false;
}

inline bool operator!=(const FieldValue& lhs, const FieldValue& rhs) {
    return !(lhs == rhs);
}

class DatabaseManager {
public:
    DatabaseManager(const std::string& catalog_path = "catalog.bin");
    ~DatabaseManager();

    // Create a new table
    bool createTable(
        const std::string& table_name,
        const std::vector<std::tuple<std::string, std::string, int>>& columns,
        const std::string& primary_key,
        const std::map<std::string, std::pair<std::string, std::string>>& foreign_keys = {}
    );

    // Insert a record into a table
    bool insertRecord(const std::string& table_name, const Record& record);

    // Search for records in a table
    std::vector<Record> searchRecords(const std::string& table_name, const std::string& key_column, const FieldValue& key_value);

    // List all tables
    std::vector<std::string> listTables() const;

    // Get schema for a specific table
    TableSchema getTableSchema(const std::string& table_name) const;
    std::vector<Record> getAllRecords(const std::string& table_name);
    std::vector<Record> searchRecordsWithFilter(
        const std::string& table_name,
        const std::vector<std::tuple<std::string, std::string, FieldValue>>& conditions,
        const std::vector<std::string>& operators);

    bool updateRecordsWithFilter(
        const std::string& table_name,
        const std::map<std::string, FieldValue>& update_values,
        const std::vector<std::tuple<std::string, std::string, FieldValue>>& conditions,
        const std::vector<std::string>& operators);

    int deleteRecordsWithFilter(
        const std::string& table_name,
        const std::vector<std::tuple<std::string, std::string, FieldValue>>& conditions,
        const std::vector<std::string>& operators);
    bool createDatabase(const std::string& db_name);
    bool dropDatabase(const std::string& db_name);
    bool useDatabase(const std::string& db_name);
    bool dropTable(const std::string& table_name);
    std::vector<std::string> listDatabases() const;
    std::string getCurrentDatabase() const;



private:
    Catalog catalog;
    std::string catalog_path;
    std::map<std::string, BPlusTree*> indexes; // Map of table name to index
    std::string current_database;

    // Helper methods
    Column::Type stringToColumnType(const std::string& type_str);
    void saveRecord(std::ofstream& file, const Record& record, const TableSchema& schema, int& offset);
    Record loadRecord(std::ifstream& file, const TableSchema& schema);
    int getFieldSize(const Column& column) const;
    void serializeField(std::ofstream& file, const FieldValue& value, const Column& column);
    FieldValue deserializeField(std::ifstream& file, const Column& column);

    // Create index for a table
    void createIndex(const TableSchema& schema);

    // Load existing indexes
    void loadIndexes();
    bool evaluateCondition(
        const Record& record,
        const std::vector<std::tuple<std::string, std::string, FieldValue>>& conditions,
        const std::vector<std::string>& operators);
    std::filesystem::path getDatabasePath(const std::string& db_name);
};

// Helper function for evaluating a single condition
bool evaluateSingleCondition(const Record& record, const std::string& column, const std::string& op, const FieldValue& value);

#endif