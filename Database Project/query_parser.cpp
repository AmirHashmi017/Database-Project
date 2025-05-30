#include "query_parser.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <iostream>

QueryParser::QueryParser(DatabaseManager& db_manager) : db_manager(db_manager) {}

bool QueryParser::parse(const std::string& query_string) {
    // Clear any existing commands
    commands.clear();
    
    // Split the query into individual commands by semicolon
    std::string current_command;
    bool in_quotes = false;
    
    // Clean up the query string
    std::string cleaned_query = query_string;
    // Replace all types of newlines and whitespace sequences with a single space
    for (size_t i = 0; i < cleaned_query.length(); ) {
        if (cleaned_query[i] == '\r' || cleaned_query[i] == '\n' || cleaned_query[i] == '\t') {
            cleaned_query[i] = ' ';
            i++;
        } else if (cleaned_query[i] == ' ' && i + 1 < cleaned_query.length() && cleaned_query[i + 1] == ' ') {
            cleaned_query.erase(i, 1);
        } else {
            i++;
        }
    }
    
    // Trim leading/trailing spaces
    while (!cleaned_query.empty() && cleaned_query[0] == ' ') {
        cleaned_query.erase(0, 1);
    }
    while (!cleaned_query.empty() && cleaned_query.back() == ' ') {
        cleaned_query.pop_back();
    }
    
    for (char c : cleaned_query) {
        if (c == '\'') {
            in_quotes = !in_quotes;
            current_command += c;
        } else if (c == ';' && !in_quotes) {
            if (!current_command.empty()) {
                commands.push_back(current_command);
                current_command.clear();
            }
        } else {
            current_command += c;
        }
    }
    
    // Add the last command if there is one
    if (!current_command.empty()) {
        commands.push_back(current_command);
    }
    
    // Process each command
    bool all_success = true;
    current_query.error_message.clear(); // Clear at start of parse
    for (const auto& cmd : commands) {
        std::vector<std::string> tokens = tokenize(cmd);
        if (tokens.empty()) continue;

        // Convert first token to uppercase for case-insensitive comparison
        std::string command = tokens[0];
        std::transform(command.begin(), command.end(), command.begin(), ::toupper);

        if (command == "CREATE") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid CREATE syntax: missing object type";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASE") {
                current_query.type = QueryType::CREATE_DATABASE;
                all_success &= parseCreateDatabase(tokens);
            } else if (object == "TABLE") {
                current_query.type = QueryType::CREATE_TABLE;
                all_success &= parseCreateTable(tokens);
            } else {
                current_query.error_message = "Invalid CREATE syntax: unknown object '" + object + "'";
                return false;
            }
        } else if (command == "DROP") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid DROP syntax: missing object type";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASE") {
                current_query.type = QueryType::DROP_DATABASE;
                all_success &= parseDropDatabase(tokens);
            } else if (object == "TABLE") {
                current_query.type = QueryType::DROP_TABLE;
                all_success &= parseDropTable(tokens);
            } else {
                current_query.error_message = "Invalid DROP syntax: unknown object '" + object + "'";
                return false;
            }
        } else if (command == "USE") {
            current_query.type = QueryType::USE_DATABASE;
            all_success &= parseUseDatabase(tokens);
        } else if (command == "SHOW") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid SHOW syntax: missing object type";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASES") {
                current_query.type = QueryType::SHOW_DATABASES;
                all_success &= true;
            } else if (object == "TABLES") {
                current_query.type = QueryType::SHOW_TABLES;
                all_success &= true;
            } else {
                current_query.error_message = "Invalid SHOW syntax: unknown object '" + object + "'";
                return false;
            }
        } else if (command == "INSERT") {
            current_query.type = QueryType::INSERT;
            all_success &= parseInsert(tokens);
        } else if (command == "SELECT") {
            current_query.type = QueryType::SELECT;
            all_success &= parseSelect(tokens);
        } else if (command == "UPDATE") {
            current_query.type = QueryType::UPDATE;
            all_success &= parseUpdate(tokens);
        } else if (command == "DELETE") {
            current_query.type = QueryType::DELETE_OP;
            all_success &= parseDelete(tokens);
        } else {
            current_query.error_message = "Unknown command: '" + command + "'";
            return false;
        }
    }
    
    return all_success;
}

bool QueryParser::execute() {
    bool success = true;
    std::vector<Record> results;
    int records_found = 0;

    for (const auto& cmd : commands) {
        std::vector<std::string> tokens = tokenize(cmd);
        if (tokens.empty()) continue;

        // Convert first token to uppercase for case-insensitive comparison
        std::string command = tokens[0];
        std::transform(command.begin(), command.end(), command.begin(), ::toupper);

        // Clear results for this command, but preserve error_message if already set
        results.clear();
        records_found = 0;

        if (command == "CREATE") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid CREATE command syntax";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASE") {
                current_query.type = QueryType::CREATE_DATABASE;
                if (!parseCreateDatabase(tokens)) {
                    current_query.error_message = "Failed to parse CREATE DATABASE command";
                    return false;
                }
                success &= db_manager.createDatabase(current_query.database_name);
                if (!success) {
                    current_query.error_message = "Failed to create database '" + current_query.database_name + "'";
                }
            } else if (object == "TABLE") {
                current_query.type = QueryType::CREATE_TABLE;
                if (!parseCreateTable(tokens)) {
                    current_query.error_message = "Failed to parse CREATE TABLE command";
                    return false;
                }
                success &= db_manager.createTable(
                    current_query.table_name,
                    current_query.columns,
                    current_query.primary_key,
                    current_query.foreign_keys
                );
                if (!success) {
                    current_query.error_message = "Failed to create table '" + current_query.table_name + "'";
                }
            }
        } else if (command == "DROP") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid DROP command syntax";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASE") {
                current_query.type = QueryType::DROP_DATABASE;
                if (!parseDropDatabase(tokens)) {
                    current_query.error_message = "Failed to parse DROP DATABASE command";
                    return false;
                }
                success &= db_manager.dropDatabase(current_query.database_name);
                if (!success) {
                    current_query.error_message = "Failed to drop database '" + current_query.database_name + "'";
                }
            } else if (object == "TABLE") {
                current_query.type = QueryType::DROP_TABLE;
                if (!parseDropTable(tokens)) {
                    current_query.error_message = "Failed to parse DROP TABLE command";
                    return false;
                }
                success &= db_manager.dropTable(current_query.table_name);
                if (!success) {
                    current_query.error_message = "Failed to drop table '" + current_query.table_name + "'";
                }
            }
        } else if (command == "USE") {
            current_query.type = QueryType::USE_DATABASE;
            if (!parseUseDatabase(tokens)) {
                current_query.error_message = "Failed to parse USE DATABASE command";
                return false;
            }
            success &= db_manager.useDatabase(current_query.database_name);
            if (!success) {
                current_query.error_message = "Failed to use database '" + current_query.database_name + "'";
            }
        } else if (command == "SHOW") {
            if (tokens.size() < 2) {
                current_query.error_message = "Invalid SHOW command syntax";
                return false;
            }
            std::string object = tokens[1];
            std::transform(object.begin(), object.end(), object.begin(), ::toupper);
            
            if (object == "DATABASES") {
                current_query.type = QueryType::SHOW_DATABASES;
                auto databases = db_manager.listDatabases();
                for (const auto& db : databases) {
                    Record record;
                    record["database"] = db;
                    results.push_back(record);
                }
                records_found = databases.size();
            } else if (object == "TABLES") {
                current_query.type = QueryType::SHOW_TABLES;
                auto tables = db_manager.listTables();
                for (const auto& table : tables) {
                    Record record;
                    record["table"] = table;
                    results.push_back(record);
                }
                records_found = tables.size();
            }
        } else if (command == "INSERT") {
            current_query.type = QueryType::INSERT;
            if (!parseInsert(tokens)) {
                current_query.error_message = "Failed to parse INSERT command";
                return false;
            }
            Record record;
            for (const auto& [key, value] : current_query.values) {
                record[key] = value;
            }
            success &= db_manager.insertRecord(current_query.table_name, record);
            if (!success) {
                current_query.error_message = "Failed to insert record into table '" + current_query.table_name + "'";
            }
        } else if (command == "SELECT") {
    current_query.type = QueryType::SELECT;
    TableSchema schema1 = db_manager.getTableSchema(current_query.table_name);
    if (schema1.name.empty()) {
        current_query.error_message = "Table '" + current_query.table_name + "' does not exist";
        return false;
    }
    if (!current_query.join_table_name.empty()) {
        TableSchema schema2 = db_manager.getTableSchema(current_query.join_table_name);
        if (schema2.name.empty()) {
            current_query.error_message = "Join table '" + current_query.join_table_name + "' does not exist";
            return false;
        }
        // Handle JOIN query
        std::vector<std::tuple<std::string, std::string, FieldValue>> join_conditions;
        for (const auto& cond : current_query.conditions) {
            join_conditions.push_back(std::make_tuple(cond.column, cond.op, cond.value));
        }
        results = db_manager.joinTables(
            current_query.table_name,
            current_query.join_table_name,
            current_query.join_condition,
            join_conditions,
            current_query.condition_operators
        );
        // Filter results to include only requested columns
        results = filterRecordsByColumns(results, current_query.select_columns);
        if (results.empty() && !join_conditions.empty()) {
            current_query.error_message = "No records match the JOIN conditions";
        }
    } else if (current_query.conditions.empty()) {
        results = db_manager.getAllRecords(current_query.table_name);
        // Filter results to include only requested columns
        results = filterRecordsByColumns(results, current_query.select_columns);
        if (results.empty()) {
            current_query.error_message = "No records found in table '" + current_query.table_name + "'";
        }
    } else {
        std::vector<std::tuple<std::string, std::string, FieldValue>> conditions;
        for (const auto& cond : current_query.conditions) {
            conditions.push_back(std::make_tuple(cond.column, cond.op, cond.value));
        }
        results = db_manager.searchRecordsWithFilter(
            current_query.table_name,
            conditions,
            current_query.condition_operators
        );
        // Filter results to include only requested columns
        results = filterRecordsByColumns(results, current_query.select_columns);
        if (results.empty()) {
            current_query.error_message = "No records match the WHERE conditions in table '" + current_query.table_name + "'";
        }
    }
    records_found = results.size();
} else if (command == "UPDATE") {
            current_query.type = QueryType::UPDATE;
            if (!parseUpdate(tokens)) {
                current_query.error_message = "Failed to parse UPDATE command";
                return false;
            }
            std::vector<std::tuple<std::string, std::string, FieldValue>> conditions;
            for (const auto& cond : current_query.conditions) {
                conditions.push_back(std::make_tuple(cond.column, cond.op, cond.value));
            }
            success &= db_manager.updateRecordsWithFilter(
                current_query.table_name,
                current_query.values,
                conditions,
                current_query.condition_operators
            );
            if (!success) {
                current_query.error_message = "Failed to update records in table '" + current_query.table_name + "'";
            }
        } else if (command == "DELETE") {
            current_query.type = QueryType::DELETE_OP;
            if (!parseDelete(tokens)) {
                current_query.error_message = "Failed to parse DELETE command";
                return false;
            }
            std::vector<std::tuple<std::string, std::string, FieldValue>> conditions;
            for (const auto& cond : current_query.conditions) {
                conditions.push_back(std::make_tuple(cond.column, cond.op, cond.value));
            }
            int deleted = db_manager.deleteRecordsWithFilter(
                current_query.table_name,
                conditions,
                current_query.condition_operators
            );
            success &= (deleted >= 0);
            records_found = deleted;
            if (!success) {
                current_query.error_message = "Failed to delete records from table '" + current_query.table_name + "'";
            }
        }
    }

    // Store the results and records found
    current_query.results = results;
    current_query.records_found = records_found;
    
    return success;
}

// Parsing methods
bool QueryParser::parseCreateDatabase(const std::vector<std::string>& tokens) {
    if (tokens.size() != 3) {
        current_query.error_message = "Invalid CREATE DATABASE syntax: expected 'CREATE DATABASE name'";
        return false;
    }
    current_query.database_name = tokens[2];
    return true;
}

bool QueryParser::parseDropDatabase(const std::vector<std::string>& tokens) {
    if (tokens.size() != 3) {
        current_query.error_message = "Invalid DROP DATABASE syntax: expected 'DROP DATABASE name'";
        return false;
    }
    current_query.database_name = tokens[2];
    return true;
}

bool QueryParser::parseUseDatabase(const std::vector<std::string>& tokens) {
    if (tokens.size() != 2) {
        current_query.error_message = "Invalid USE DATABASE syntax: expected 'USE name'";
        return false;
    }
    current_query.database_name = tokens[1];
    return true;
}

bool QueryParser::parseCreateTable(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4) {
        current_query.error_message = "Invalid CREATE TABLE syntax: expected 'CREATE TABLE name (...)'";
        return false;
    }
    
    current_query.type = QueryType::CREATE_TABLE;
    current_query.table_name = tokens[2];
    
    // Parse column definitions and constraints
    std::vector<std::tuple<std::string, std::string, int>> columns;
    std::string primary_key;
    std::map<std::string, std::pair<std::string, std::string>> foreign_keys;
    
    // Find the opening parenthesis
    size_t i = 3;
    while (i < tokens.size() && tokens[i] != "(") {
        i++;
    }
    if (i >= tokens.size()) {
        current_query.error_message = "Expected '(' after table name";
        return false;
    }
    i++; // Skip the opening parenthesis
    
    std::string current_col_name;
    std::string current_col_type;
    int current_col_length = 0; // Default to 0 (no length specified)
    bool is_primary_key = false;
    
    while (i < tokens.size() && tokens[i] != ")") {
        std::string token = tokens[i];
        if (token.empty()) {
            i++;
            continue;
        }
        
        std::string token_upper = token;
        std::transform(token_upper.begin(), token_upper.end(), token_upper.begin(), ::toupper);
        
        // Handle PRIMARY KEY declaration
        if (token_upper == "PRIMARY" && i + 3 < tokens.size() && 
            tokens[i + 1] == "KEY" && tokens[i + 2] == "(") {
            if (tokens[i + 3] == ")") {
                current_query.error_message = "PRIMARY KEY column name missing";
                return false;
            }
            primary_key = tokens[i + 3];
            i += 5; // Skip PRIMARY KEY (column_name)
            if (i < tokens.size() && tokens[i] == ",") {
                i++; // Skip comma if present
            }
            continue;
        }
        
        // Handle FOREIGN KEY constraint
        if (token_upper == "FOREIGN" && i + 6 < tokens.size() && 
            tokens[i + 1] == "KEY" && tokens[i + 2] == "(" && tokens[i + 4] == ")" && 
            tokens[i + 5] == "REFERENCES") {
            std::string local_column = tokens[i + 3];
            std::string ref_table = tokens[i + 6];
            std::string ref_column;
            
            if (i + 9 < tokens.size() && tokens[i + 7] == "(" && tokens[i + 9] == ")") {
                ref_column = tokens[i + 8];
                i += 10; // Skip FOREIGN KEY (col) REFERENCES table(col)
            } else {
                ref_column = local_column; // Default to same column name
                i += 7; // Skip FOREIGN KEY (col) REFERENCES table
            }
            
            foreign_keys[local_column] = std::make_pair(ref_table, ref_column);
            if (i < tokens.size() && tokens[i] == ",") {
                i++; // Skip comma if present
            }
            continue;
        }
        
        // Handle column definition
        if (current_col_name.empty()) {
            current_col_name = token;
            i++;
            continue;
        }
        
        if (current_col_type.empty()) {
            current_col_type = token_upper;
            // Handle types with length specifications (STRING, CHAR)
            if ((current_col_type == "STRING" || current_col_type == "CHAR") && 
                i + 3 < tokens.size() && tokens[i + 1] == "(" && tokens[i + 3] == ")") {
                try {
                    current_col_length = std::stoi(tokens[i + 2]);
                    i += 4; // Skip type ( length )
                } catch (...) {
                    current_query.error_message = "Invalid length for " + current_col_type;
                    return false;
                }
            } else {
                i++; // Skip type
            }
            
            // Look ahead for PRIMARY KEY or comma
            if (i < tokens.size()) {
                std::string next_token = tokens[i];
                std::transform(next_token.begin(), next_token.end(), next_token.begin(), ::toupper);
                
                if (next_token == "PRIMARY" && i + 1 < tokens.size() && tokens[i + 1] == "KEY") {
                    is_primary_key = true;
                    i += 2; // Skip PRIMARY KEY
                }
                
                if (i < tokens.size() && (tokens[i] == "," || tokens[i] == ")")) {
                    // End of column definition
                    columns.push_back(std::make_tuple(current_col_name, current_col_type, current_col_length));
                    if (is_primary_key) {
                        primary_key = current_col_name;
                        is_primary_key = false;
                    }
                    current_col_name.clear();
                    current_col_type.clear();
                    current_col_length = 0;
                    if (tokens[i] == ",") {
                        i++; // Skip comma
                    }
                }
            }
            continue;
        }
        
        i++;
    }
    
    // Add the last column if it exists and hasn't been added
    if (!current_col_name.empty() && !current_col_type.empty()) {
        columns.push_back(std::make_tuple(current_col_name, current_col_type, current_col_length));
        if (is_primary_key) {
            primary_key = current_col_name;
        }
    }
    
    if (columns.empty()) {
        current_query.error_message = "No columns defined for table";
        return false;
    }
    
    // Validate primary key
    bool pk_found = false;
    for (const auto& col : columns) {
        if (std::get<0>(col) == primary_key) {
            pk_found = true;
            break;
        }
    }
    if (!primary_key.empty() && !pk_found) {
        current_query.error_message = "Primary key column '" + primary_key + "' not found in column definitions";
        return false;
    }
    
    current_query.columns = columns;
    current_query.primary_key = primary_key;
    current_query.foreign_keys = foreign_keys;
    
    return true;
}

bool QueryParser::parseDropTable(const std::vector<std::string>& tokens) {
    if (tokens.size() != 3) {
        current_query.error_message = "Invalid DROP TABLE syntax: expected 'DROP TABLE name'";
        return false;
    }
    current_query.table_name = tokens[2];
    return true;
}

bool QueryParser::parseInsert(const std::vector<std::string>& tokens) {
    if (tokens.size() < 6) {
        current_query.error_message = "Invalid INSERT syntax: expected 'INSERT INTO table VALUES (...)'";
        return false;
    }

    current_query.type = QueryType::INSERT;
    current_query.table_name = tokens[2];

    // Parse values
    std::map<std::string, FieldValue> values;
    int value_index = 0;

    // Get table schema to map values to column names
    TableSchema schema = db_manager.getTableSchema(current_query.table_name);
    if (schema.name.empty()) {
        current_query.error_message = "Table '" + current_query.table_name + "' does not exist";
        return false;
    }

    // Start parsing after VALUES keyword
    size_t i = 4; // Should point to '(' after VALUES
    if (tokens[i] != "(") {
        current_query.error_message = "Expected '(' after VALUES";
        return false;
    }
    i++; // Skip '('

    std::vector<std::string> value_tokens;
    bool in_quotes = false;
    std::string current_token;

    // Collect value tokens, preserving quoted strings
    while (i < tokens.size() && tokens[i] != ")") {
        std::string token = tokens[i];

        if (token == "," && !in_quotes) {
            if (!current_token.empty()) {
                value_tokens.push_back(current_token);
                current_token.clear();
            }
            i++;
            continue;
        }

        if (token == "'" && !in_quotes) {
            in_quotes = true;
            current_token += token;
            i++;
            continue;
        }

        if (token == "'" && in_quotes) {
            in_quotes = false;
            current_token += token;
            i++;
            continue;
        }

        current_token += token;
        if (!in_quotes && i + 1 < tokens.size() && tokens[i + 1] != "," && tokens[i + 1] != ")") {
            current_token += " ";
        }
        i++;
    }

    if (!current_token.empty()) {
        value_tokens.push_back(current_token);
    }

    if (i >= tokens.size() || tokens[i] != ")") {
        current_query.error_message = "Expected ')' after values";
        return false;
    }

    // Parse each value token
    for (const auto& token : value_tokens) {
        if (value_index >= schema.columns.size()) {
            current_query.error_message = "Too many values for table '" + current_query.table_name + "'";
            return false;
        }

        const std::string& column_name = schema.columns[value_index].name;
        Column::Type column_type = schema.columns[value_index].type;

        try {
            switch (column_type) {
            case Column::INT: {
                int int_val = std::stoi(token);
                values[column_name] = int_val;
                break;
            }
            case Column::FLOAT: {
                float float_val = std::stof(token);
                values[column_name] = float_val;
                break;
            }
            case Column::STRING:
            case Column::CHAR: {
                std::string str_val = token;
                if (str_val.front() == '\'' && str_val.back() == '\'') {
                    str_val = str_val.substr(1, str_val.length() - 2);
                }
                values[column_name] = str_val;
                break;
            }
            case Column::BOOL: {
                bool bool_val = (token == "true" || token == "TRUE" || token == "1");
                values[column_name] = bool_val;
                break;
            }
            }
            value_index++;
        }
        catch (...) {
            current_query.error_message = "Invalid value '" + token + "' for column '" + column_name + "'";
            return false;
        }
    }

    if (value_index != schema.columns.size()) {
        current_query.error_message = "Incorrect number of values for table '" + current_query.table_name + "'";
        return false;
    }

    current_query.values = values;
    return true;
}

bool QueryParser::parseSelect(const std::vector<std::string>& tokens) {
    if (tokens.size() < 4) {
        current_query.error_message = "Invalid SELECT syntax: expected 'SELECT ... FROM table'";
        return false;
    }
    current_query.type = QueryType::SELECT;
    size_t from_pos = std::find(tokens.begin(), tokens.end(), "FROM") - tokens.begin();
    if (from_pos == tokens.size()) {
        current_query.error_message = "Missing FROM clause";
        return false;
    }
    std::vector<std::string> columns;
    for (size_t i = 1; i < from_pos; i++) {
        std::string col = tokens[i];
        col.erase(std::remove(col.begin(), col.end(), ','), col.end());
        if (!col.empty()) {
            columns.push_back(col);
        }
    }
    if (columns.empty()) {
        columns.push_back("*");
    }
    if (from_pos + 1 >= tokens.size()) {
        current_query.error_message = "Missing table name after FROM";
        return false;
    }
    current_query.table_name = tokens[from_pos + 1];
    size_t join_pos = std::find(tokens.begin(), tokens.end(), "JOIN") - tokens.begin();
    if (join_pos < tokens.size()) {
        if (join_pos + 1 >= tokens.size()) {
            current_query.error_message = "Missing join table name";
            return false;
        }
        current_query.join_table_name = tokens[join_pos + 1];
        size_t on_pos = std::find(tokens.begin(), tokens.end(), "ON") - tokens.begin();
        if (on_pos == tokens.size()) {
            current_query.error_message = "Missing ON clause";
            return false;
        }
        if (on_pos + 3 >= tokens.size() || tokens[on_pos + 2] != "=") {
            current_query.error_message = "Invalid ON condition: expected 'table1.col = table2.col'";
            return false;
        }
        std::string left_col = tokens[on_pos + 1]; // table1.col1
        std::string right_col = tokens[on_pos + 3]; // table2.col2
        if (left_col.find('.') == std::string::npos || right_col.find('.') == std::string::npos) {
            current_query.error_message = "ON condition must specify table.column";
            return false;
        }
        Condition join_cond;
        join_cond.column = left_col;
        join_cond.op = "=";
        join_cond.value = right_col; // Store as string, resolved in joinTables
        current_query.join_condition = join_cond;
    }
    TableSchema schema1 = db_manager.getTableSchema(current_query.table_name);
    if (schema1.name.empty()) {
        current_query.error_message = "Table '" + current_query.table_name + "' does not exist";
        return false;
    }
    if (!current_query.join_table_name.empty()) {
        TableSchema schema2 = db_manager.getTableSchema(current_query.join_table_name);
        if (schema2.name.empty()) {
            current_query.error_message = "Join table '" + current_query.join_table_name + "' does not exist";
            return false;
        }
    }
    if (columns[0] != "*") {
        for (const auto& col : columns) {
            bool found = false;
            for (const auto& schema_col : schema1.columns) {
                if (schema_col.name == col || (current_query.table_name + "." + schema_col.name) == col) {
                    found = true;
                    break;
                }
            }
            if (!found && !current_query.join_table_name.empty()) {
                TableSchema schema2 = db_manager.getTableSchema(current_query.join_table_name);
                for (const auto& schema_col : schema2.columns) {
                    if (schema_col.name == col || (current_query.join_table_name + "." + schema_col.name) == col) {
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                current_query.error_message = "Column '" + col + "' does not exist in table '" +
                                             current_query.table_name + "' or '" + current_query.join_table_name + "'";
                return false;
            }
        }
    }
    current_query.select_columns = columns;
    size_t where_pos = std::find(tokens.begin(), tokens.end(), "WHERE") - tokens.begin();
    if (where_pos < tokens.size()) {
        current_query.conditions.clear();
        current_query.condition_operators.clear();
        for (size_t i = where_pos + 1; i < tokens.size(); ) {
            std::string token = tokens[i];
            std::transform(token.begin(), token.end(), token.begin(), ::toupper);
            if (token == "AND" || token == "OR" || token == "NOT") {
                current_query.condition_operators.push_back(token);
                i++;
                continue;
            }
            if (i + 2 >= tokens.size()) {
                current_query.error_message = "Incomplete WHERE condition";
                return false;
            }
            Condition cond;
            cond.column = tokens[i];
            cond.op = tokens[i + 1];
            cond.value = parseValue(tokens[i + 2]);
            current_query.conditions.push_back(cond);
            i += 3;
        }
        size_t expected_ops = current_query.conditions.size() - 1;
        size_t not_count = std::count(current_query.condition_operators.begin(),
                                     current_query.condition_operators.end(), "NOT");
        if (current_query.condition_operators.size() < expected_ops ||
            current_query.condition_operators.size() > expected_ops + not_count) {
            current_query.error_message = "Mismatched operators (" +
                                         std::to_string(current_query.condition_operators.size()) +
                                         ") for conditions (" + std::to_string(current_query.conditions.size()) + ")";
            return false;
        }
    }
    return true;
}

bool QueryParser::parseUpdate(const std::vector<std::string>& tokens) {
    if (tokens.size() < 6) {
        current_query.error_message = "Invalid UPDATE syntax: expected 'UPDATE table SET ...'";
        return false;
    }
    
    current_query.type = QueryType::UPDATE;
    current_query.table_name = tokens[1];
    
    // Find SET keyword
    size_t set_pos = std::find(tokens.begin(), tokens.end(), "SET") - tokens.begin();
    if (set_pos == tokens.size()) {
        current_query.error_message = "Missing SET clause";
        return false;
    }
    
    // Parse SET assignments
    std::map<std::string, FieldValue> values;
    for (size_t i = set_pos + 1; i < tokens.size(); i++) {
        if (tokens[i] == "WHERE") break;
        if (i + 2 >= tokens.size() || tokens[i + 1] != "=") continue;
        
        values[tokens[i]] = parseValue(tokens[i + 2]);
        i += 2;
    }
    current_query.values = values;
    
    // Parse WHERE conditions if present
    size_t where_pos = std::find(tokens.begin(), tokens.end(), "WHERE") - tokens.begin();
    if (where_pos < tokens.size()) {
        current_query.conditions.clear();
        current_query.condition_operators.clear();
        
        for (size_t i = where_pos + 1; i < tokens.size(); ) {
            std::string token = tokens[i];
            std::transform(token.begin(), token.end(), token.begin(), ::toupper);
            
            if (token == "AND" || token == "OR" || token == "NOT") {
                current_query.condition_operators.push_back(token);
                i++;
                continue;
            }
            
            if (i + 2 >= tokens.size()) {
                current_query.error_message = "Incomplete WHERE condition";
                return false;
            }
            
            Condition cond;
            cond.column = tokens[i];
            cond.op = tokens[i + 1];
            cond.value = parseValue(tokens[i + 2]);
            current_query.conditions.push_back(cond);
            i += 3;
        }
        
        // Validate operator count
        size_t expected_ops = current_query.conditions.size() - 1;
        size_t not_count = std::count(current_query.condition_operators.begin(), 
                                     current_query.condition_operators.end(), "NOT");
        if (current_query.condition_operators.size() < expected_ops || 
            current_query.condition_operators.size() > expected_ops + not_count) {
            current_query.error_message = "Mismatched operators (" +
                                         std::to_string(current_query.condition_operators.size()) +
                                         ") for conditions (" + std::to_string(current_query.conditions.size()) + ")";
            return false;
        }
    }
    
    return true;
}

bool QueryParser::parseDelete(const std::vector<std::string>& tokens) {
    if (tokens.size() < 3) {
        current_query.error_message = "Invalid DELETE syntax: expected 'DELETE FROM table'";
        return false;
    }
    
    current_query.type = QueryType::DELETE_OP;
    current_query.table_name = tokens[2];
    
    // Parse WHERE conditions if present
    size_t where_pos = std::find(tokens.begin(), tokens.end(), "WHERE") - tokens.begin();
    if (where_pos < tokens.size()) {
        current_query.conditions.clear();
        current_query.condition_operators.clear();
        
        for (size_t i = where_pos + 1; i < tokens.size(); ) {
            std::string token = tokens[i];
            std::transform(token.begin(), token.end(), token.begin(), ::toupper);
            
            if (token == "AND" || token == "OR" || token == "NOT") {
                current_query.condition_operators.push_back(token);
                i++;
                continue;
            }
            
            if (i + 2 >= tokens.size()) {
                current_query.error_message = "Incomplete WHERE condition";
                return false;
            }
            
            Condition cond;
            cond.column = tokens[i];
            cond.op = tokens[i + 1];
            cond.value = parseValue(tokens[i + 2]);
            current_query.conditions.push_back(cond);
            i += 3;
        }
        
        // Validate operator count
        size_t expected_ops = current_query.conditions.size() - 1;
        size_t not_count = std::count(current_query.condition_operators.begin(), 
                                     current_query.condition_operators.end(), "NOT");
        if (current_query.condition_operators.size() < expected_ops || 
            current_query.condition_operators.size() > expected_ops + not_count) {
            current_query.error_message = "Mismatched operators (" +
                                         std::to_string(current_query.condition_operators.size()) +
                                         ") for conditions (" + std::to_string(current_query.conditions.size()) + ")";
            return false;
        }
    }
    
    return true;
}

// Helper methods
std::vector<std::string> QueryParser::tokenize(const std::string& query) {
    std::vector<std::string> tokens;
    std::string current_token;
    bool in_quotes = false;
    
    // Clean up the query string first
    std::string cleaned_query = query;
    // Replace all types of newlines and whitespace sequences with a single space
    for (size_t i = 0; i < cleaned_query.length(); ) {
        if (cleaned_query[i] == '\r' || cleaned_query[i] == '\n' || cleaned_query[i] == '\t') {
            cleaned_query[i] = ' ';
            i++;
        } else if (cleaned_query[i] == ' ' && i + 1 < cleaned_query.length() && cleaned_query[i + 1] == ' ') {
            cleaned_query.erase(i, 1);
        } else {
            i++;
        }
    }
    
    // Trim leading/trailing spaces
    while (!cleaned_query.empty() && cleaned_query[0] == ' ') {
        cleaned_query.erase(0, 1);
    }
    while (!cleaned_query.empty() && cleaned_query.back() == ' ') {
        cleaned_query.pop_back();
    }
    
    // Enhanced tokenization to handle delimiters
    for (size_t i = 0; i < cleaned_query.length(); ) {
        char c = cleaned_query[i];
        
        if (c == '\'') {
            in_quotes = !in_quotes;
            current_token += c;
            i++;
        } else if (!in_quotes && (c == '(' || c == ')' || c == ',' || c == ';')) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            tokens.push_back(std::string(1, c));
            i++;
        } else if (!in_quotes && isspace(c)) {
            if (!current_token.empty()) {
                tokens.push_back(current_token);
                current_token.clear();
            }
            i++;
        } else {
            current_token += c;
            i++;
        }
    }
    
    if (!current_token.empty()) {
        tokens.push_back(current_token);
    }
    
    // Debug: Print tokens
    std::cout << "Tokenized query: ";
    for (const auto& token : tokens) {
        std::cout << "'" << token << "' ";
    }
    std::cout << std::endl;
    
    return tokens;
}

FieldValue QueryParser::parseValue(const std::string& value_str) {
    // Try to parse as int
    try {
        return std::stoi(value_str);
    } catch (...) {}
    
    // Try to parse as float
    try {
        return std::stof(value_str);
    } catch (...) {}
    
    // Check for boolean
    if (value_str == "true" || value_str == "TRUE") return true;
    if (value_str == "false" || value_str == "FALSE") return false;
    
    // Treat as string (remove quotes if present)
    std::string str = value_str;
    if (str.front() == '\'' && str.back() == '\'') {
        str = str.substr(1, str.length() - 2);
    }
    return str;
}

Column::Type QueryParser::parseColumnType(const std::string& type_str) {
    std::string type = type_str;
    std::transform(type.begin(), type.end(), type.begin(), ::toupper);
    
    if (type == "INT") return Column::INT;
    if (type == "FLOAT") return Column::FLOAT;
    if (type == "STRING") return Column::STRING;
    if (type == "CHAR") return Column::CHAR;
    if (type == "BOOL") return Column::BOOL;
    
    throw std::runtime_error("Unknown column type: " + type_str);
}
std::vector<Record> QueryParser::filterRecordsByColumns(const std::vector<Record>& records, const std::vector<std::string>& columns) {
    if (columns.size() == 1 && columns[0] == "*") {
        return records; // Return all columns if "*" is specified
    }

    std::vector<Record> filtered_records;
    for (const auto& record : records) {
        Record filtered_record;
        for (const auto& col : columns) {
            // Use the full column name from the query (e.g., "users.name", "orders.order_id")
            std::string full_col_name = col;
            // Try the full column name first (e.g., "users.name")
            if (record.find(full_col_name) != record.end()) {
                filtered_record[full_col_name] = record.at(full_col_name);
                continue;
            }
            // Try the base column name (e.g., "name", "order_id")
            size_t dot_pos = col.find('.');
            if (dot_pos != std::string::npos) {
                std::string base_col_name = col.substr(dot_pos + 1);
                if (record.find(base_col_name) != record.end()) {
                    filtered_record[full_col_name] = record.at(base_col_name);
                }
            }
        }
        // Only include non-empty records
        if (!filtered_record.empty()) {
            filtered_records.push_back(filtered_record);
        }
    }
    return filtered_records;
}