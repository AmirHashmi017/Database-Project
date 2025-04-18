#include "database_manager.h"

// Get the executable path helper function
std::string getExecutablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
#else
    char buffer[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", buffer, PATH_MAX);
    return std::string(buffer, (count > 0) ? count : 0).substr(0,
        std::string(buffer).find_last_of("/"));
#endif
}

DatabaseManager::DatabaseManager(const std::string& catalog_path_rel) {
    // Get executable directory and create data directory
    std::string exePath = getExecutablePath();
    std::filesystem::path dataDir = std::filesystem::path(exePath) / "data";
    std::filesystem::create_directories(dataDir);

    // Set absolute path for catalog
    this->catalog_path = (dataDir / std::filesystem::path(catalog_path_rel).filename()).string();

    std::cout << "Using catalog path: " << this->catalog_path << std::endl;

    // Load the catalog if it exists
    catalog.load(this->catalog_path);

    // Load existing indexes
    loadIndexes();
}

DatabaseManager::~DatabaseManager() {
    // Save catalog
    catalog.save(catalog_path);

    // Clean up indexes
    for (auto& [name, index] : indexes) {
        delete index;
    }
    indexes.clear();
}

bool DatabaseManager::createTable(
    const std::string& table_name,
    const std::vector<std::tuple<std::string, std::string, int>>& columns,
    const std::string& primary_key,
    const std::map<std::string, std::pair<std::string, std::string>>& foreign_keys) {

    // Check if table already exists
    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            std::cerr << "Table '" << table_name << "' already exists" << std::endl;
            return false;
        }
    }

    // Create new table schema
    TableSchema table;
    table.name = table_name;

    // Add columns
    for (const auto& [col_name, col_type, col_length] : columns) {
        Column column;
        column.name = col_name;
        column.type = stringToColumnType(col_type);
        column.length = col_length;
        column.is_primary_key = (col_name == primary_key);
        column.is_foreign_key = foreign_keys.find(col_name) != foreign_keys.end();

        // Set references if it's a foreign key
        if (column.is_foreign_key) {
            const auto& [ref_table, ref_column] = foreign_keys.at(col_name);
            column.references_table = ref_table;
            column.references_column = ref_column;
        }

        table.columns.push_back(column);
    }

    // Create data and index file paths using the same base directory as catalog
    std::filesystem::path baseDir = std::filesystem::path(catalog_path).parent_path();
    table.data_file_path = (baseDir / (table_name + ".dat")).string();
    table.index_file_path = (baseDir / (table_name + ".idx")).string();

    std::cout << "Creating table files at: " << baseDir << std::endl;
    std::cout << "  Data file: " << table.data_file_path << std::endl;
    std::cout << "  Index file: " << table.index_file_path << std::endl;

    // Add table to catalog
    catalog.tables.push_back(table);

    // Save catalog
    catalog.save(catalog_path);

    // Create index for primary key
    createIndex(table);

    return true;
}

void DatabaseManager::createIndex(const TableSchema& schema) {
    // Find primary key column
    Column primary_key_column;
    bool found = false;

    for (const auto& column : schema.columns) {
        if (column.is_primary_key) {
            primary_key_column = column;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "No primary key found for table '" << schema.name << "'" << std::endl;
        return;
    }

    // Make sure the directory exists
    std::filesystem::path indexPath(schema.index_file_path);
    std::filesystem::create_directories(indexPath.parent_path());

    // Create B+ tree index
    auto* index = new BPlusTree(schema.index_file_path);
    indexes[schema.name] = index;
}

void DatabaseManager::loadIndexes() {
    for (const auto& table : catalog.tables) {
        std::cout << "Loading index for table " << table.name << " from " << table.index_file_path << std::endl;

        // Check if the index file exists
        if (std::filesystem::exists(table.index_file_path)) {
            auto* index = new BPlusTree(table.index_file_path);
            indexes[table.name] = index;
        }
        else {
            std::cout << "Index file doesn't exist yet. Will be created on first insert." << std::endl;
            createIndex(table);
        }
    }
}



bool DatabaseManager::insertRecord(const std::string& table_name, const Record& record) {
    // Find the table
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return false;
    }

    // Get the primary key value
    int primary_key_value = 0;
    std::string primary_key_column;

    for (const auto& column : schema.columns) {
        if (column.is_primary_key) {
            primary_key_column = column.name;
            if (record.find(primary_key_column) == record.end()) {
                std::cerr << "Record is missing primary key '" << primary_key_column << "'" << std::endl;
                return false;
            }

            if (column.type == Column::INT) {
                primary_key_value = std::get<int>(record.at(primary_key_column));
            }
            else {
                std::cerr << "Primary key must be an integer" << std::endl;
                return false;
            }

            break;
        }
    }

    // Ensure the index exists
    if (indexes.find(table_name) == indexes.end()) {
        createIndex(schema);
    }

    // Make sure directories exist
    std::filesystem::path dataFilePath(schema.data_file_path);
    std::filesystem::create_directories(dataFilePath.parent_path());

    // Open data file for appending
    std::ofstream data_file(schema.data_file_path, std::ios::binary | std::ios::app);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return false;
    }

    // Get current position in file (will be the record offset)
    int offset = data_file.tellp();

    // Save the record
    saveRecord(data_file, record, schema, offset);

    // Index the record
    indexes[table_name]->insert(primary_key_value, offset);

    return true;
}

std::vector<Record> DatabaseManager::searchRecords(const std::string& table_name, const std::string& key_column, const FieldValue& key_value) {
    std::vector<Record> results;

    // Find the table
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return results;
    }

    // Check if the key column is the primary key
    bool is_primary_key = false;
    for (const auto& column : schema.columns) {
        if (column.name == key_column && column.is_primary_key) {
            is_primary_key = true;
            break;
        }
    }

    // Check if data file exists
    if (!std::filesystem::exists(schema.data_file_path)) {
        std::cerr << "Data file not found: " << schema.data_file_path << std::endl;
        return results;
    }

    // Open data file for reading
    std::ifstream data_file(schema.data_file_path, std::ios::binary);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return results;
    }

    // If searching by primary key and index exists, use it
    if (is_primary_key && indexes.find(table_name) != indexes.end()) {
        int key_int = std::get<int>(key_value);
        auto offsets = indexes[table_name]->search(key_int);

        for (int offset : offsets) {
            data_file.seekg(offset);
            results.push_back(loadRecord(data_file, schema));
        }
    }
    else {
        // Sequential scan
        data_file.seekg(0, std::ios::end);
        size_t file_size = data_file.tellg();
        data_file.seekg(0);

        while (data_file.tellg() < file_size) {
            size_t record_start = data_file.tellg();
            Record record = loadRecord(data_file, schema);

            // Check if this record matches the search criteria
            if (record.find(key_column) != record.end() && record[key_column] == key_value) {
                results.push_back(record);
            }
        }
    }

    return results;
}

std::vector<std::string> DatabaseManager::listTables() const {
    std::vector<std::string> table_names;
    for (const auto& table : catalog.tables) {
        table_names.push_back(table.name);
    }
    return table_names;
}

TableSchema DatabaseManager::getTableSchema(const std::string& table_name) const {
    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            return table;
        }
    }
    return TableSchema(); // Empty schema if not found
}

Column::Type DatabaseManager::stringToColumnType(const std::string& type_str) {
    if (type_str == "int") return Column::INT;
    if (type_str == "float") return Column::FLOAT;
    if (type_str == "string") return Column::STRING;
    if (type_str == "char") return Column::CHAR;
    if (type_str == "bool") return Column::BOOL;

    // Default to string
    std::cerr << "Unknown type '" << type_str << "', defaulting to STRING" << std::endl;
    return Column::STRING;
}

void DatabaseManager::saveRecord(std::ofstream& file, const Record& record, const TableSchema& schema, int& offset) {
    // Store current position as record start
    offset = file.tellp();

    // Write each field according to schema
    for (const auto& column : schema.columns) {
        if (record.find(column.name) != record.end()) {
            serializeField(file, record.at(column.name), column);
        }
        else {
            // Write default value if field is missing
            FieldValue default_value;
            switch (column.type) {
            case Column::INT: default_value = 0; break;
            case Column::FLOAT: default_value = 0.0f; break;
            case Column::STRING:
            case Column::CHAR: default_value = std::string(""); break;
            case Column::BOOL: default_value = false; break;
            }
            serializeField(file, default_value, column);
        }
    }
}

Record DatabaseManager::loadRecord(std::ifstream& file, const TableSchema& schema) {
    Record record;

    for (const auto& column : schema.columns) {
        record[column.name] = deserializeField(file, column);
    }

    return record;
}

int DatabaseManager::getFieldSize(const Column& column) const {
    switch (column.type) {
    case Column::INT: return sizeof(int);
    case Column::FLOAT: return sizeof(float);
    case Column::STRING: return sizeof(int) + column.length; // Length + max chars
    case Column::CHAR: return column.length;
    case Column::BOOL: return sizeof(bool);
    default: return 0;
    }
}

void DatabaseManager::serializeField(std::ofstream& file, const FieldValue& value, const Column& column) {
    switch (column.type) {
    case Column::INT: {
        int val = std::get<int>(value);
        file.write(reinterpret_cast<const char*>(&val), sizeof(val));
        break;
    }
    case Column::FLOAT: {
        float val = std::get<float>(value);
        file.write(reinterpret_cast<const char*>(&val), sizeof(val));
        break;
    }
    case Column::STRING: {
        std::string val = std::get<std::string>(value);
        // Truncate or pad string to fit column length
        val.resize(column.length, '\0');
        int len = val.size();
        file.write(reinterpret_cast<const char*>(&len), sizeof(len));
        file.write(val.c_str(), len);
        break;
    }
    case Column::CHAR: {
        std::string val = std::get<std::string>(value);
        // Truncate or pad string to fit column length
        val.resize(column.length, '\0');
        file.write(val.c_str(), column.length);
        break;
    }
    case Column::BOOL: {
        bool val = std::get<bool>(value);
        file.write(reinterpret_cast<const char*>(&val), sizeof(val));
        break;
    }
    }
}

FieldValue DatabaseManager::deserializeField(std::ifstream& file, const Column& column) {
    switch (column.type) {
    case Column::INT: {
        int val;
        file.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }
    case Column::FLOAT: {
        float val;
        file.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }
    case Column::STRING: {
        int len;
        file.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string val(len, '\0');
        file.read(&val[0], len);
        return val;
    }
    case Column::CHAR: {
        std::string val(column.length, '\0');
        file.read(&val[0], column.length);
        return val;
    }
    case Column::BOOL: {
        bool val;
        file.read(reinterpret_cast<char*>(&val), sizeof(val));
        return val;
    }
    default:
        return 0; // Default to int 0
    }
}

std::vector<Record> DatabaseManager::getAllRecords(const std::string& table_name) {
    std::vector<Record> results;

    // Find the table schema
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return results;
    }

    // Check if data file exists
    if (!std::filesystem::exists(schema.data_file_path)) {
        std::cerr << "Data file not found: " << schema.data_file_path << std::endl;
        return results;
    }

    // Open data file for reading
    std::ifstream data_file(schema.data_file_path, std::ios::binary);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return results;
    }

    // Read all records from the data file
    data_file.seekg(0, std::ios::end);
    size_t file_size = data_file.tellg();
    data_file.seekg(0);

    while (data_file.tellg() < file_size && data_file.good()) {
        results.push_back(loadRecord(data_file, schema));
    }

    return results;
}

// Add these implementations to your DatabaseManager.cpp file

bool DatabaseManager::updateRecord(const std::string& table_name,
    const std::string& key_column,
    const FieldValue& key_value,
    const Record& new_values) {
    // Find the table
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return false;
    }

    // Check if data file exists
    if (!std::filesystem::exists(schema.data_file_path)) {
        std::cerr << "Data file not found: " << schema.data_file_path << std::endl;
        return false;
    }

    // Open data file for reading and temp file for writing
    std::ifstream data_file(schema.data_file_path, std::ios::binary);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return false;
    }

    // Create a temp file for rewriting data
    std::string temp_file_path = schema.data_file_path + ".tmp";
    std::ofstream temp_file(temp_file_path, std::ios::binary);
    if (!temp_file) {
        std::cerr << "Failed to create temporary file: " << temp_file_path << std::endl;
        return false;
    }

    // Get primary key column for index update
    std::string primary_key_column;
    for (const auto& column : schema.columns) {
        if (column.is_primary_key) {
            primary_key_column = column.name;
            break;
        }
    }

    // Find and update records
    data_file.seekg(0, std::ios::end);
    size_t file_size = data_file.tellg();
    data_file.seekg(0);

    bool record_found = false;
    bool primary_key_changed = false;
    int old_primary_key = 0;
    int new_primary_key = 0;
    std::map<int, int> offset_map; // Maps old positions to new positions

    while (data_file.tellg() < file_size && data_file.good()) {
        int old_offset = data_file.tellg();
        Record record = loadRecord(data_file, schema);
        int new_offset = temp_file.tellp();

        // Check if this record matches the search criteria
        bool matches = false;
        if (record.find(key_column) != record.end() && record[key_column] == key_value) {
            matches = true;
            record_found = true;

            // If updating primary key, remember old and new values for index update
            if (primary_key_column != "" &&
                new_values.find(primary_key_column) != new_values.end()) {
                primary_key_changed = true;
                old_primary_key = std::get<int>(record[primary_key_column]);
                new_primary_key = std::get<int>(new_values.at(primary_key_column));
            }

            // Update the record with new values
            for (const auto& [col, val] : new_values) {
                record[col] = val;
            }
        }

        // Write record to temp file
        saveRecord(temp_file, record, schema, new_offset);
        offset_map[old_offset] = new_offset;
    }

    // Close both files
    data_file.close();
    temp_file.close();

    if (!record_found) {
        // No matching record found, delete temp file
        std::filesystem::remove(temp_file_path);
        std::cerr << "No record found matching the criteria" << std::endl;
        return false;
    }

    // Replace original file with temp file
    try {
        std::filesystem::rename(temp_file_path, schema.data_file_path);
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error replacing file: " << e.what() << std::endl;
        return false;
    }

    // Update index if primary key changed
    if (primary_key_changed && indexes.find(table_name) != indexes.end()) {
        // Remove old entry and add new one
        BPlusTree* index = indexes[table_name];

        // We need to update the index - unfortunately we need to rebuild it
        // as our B+ tree doesn't support direct updates
        std::string index_file_path = schema.index_file_path + ".tmp";
        BPlusTree* new_index = new BPlusTree(index_file_path);

        // Get all indexed values from current index
        // This is inefficient but workable for this implementation
        std::vector<Record> all_records = getAllRecords(table_name);

        for (const auto& record : all_records) {
            if (record.find(primary_key_column) != record.end()) {
                int key = std::get<int>(record.at(primary_key_column));
                int offset = offset_map[index->search(key)[0]]; // Get the new offset
                new_index->insert(key, offset);
            }
        }

        // Close both indexes
        delete index;
        delete new_index;

        // Replace old index with new one
        try {
            std::filesystem::rename(index_file_path, schema.index_file_path);
            // Reload the index
            indexes[table_name] = new BPlusTree(schema.index_file_path);
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error replacing index file: " << e.what() << std::endl;
            return false;
        }
    }

    return true;
}

bool DatabaseManager::deleteRecord(const std::string& table_name,
    const std::string& key_column,
    const FieldValue& key_value) {
    // Find the table
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return false;
    }

    // Check if data file exists
    if (!std::filesystem::exists(schema.data_file_path)) {
        std::cerr << "Data file not found: " << schema.data_file_path << std::endl;
        return false;
    }

    // Open data file for reading and temp file for writing
    std::ifstream data_file(schema.data_file_path, std::ios::binary);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return false;
    }

    // Create a temp file for rewriting data
    std::string temp_file_path = schema.data_file_path + ".tmp";
    std::ofstream temp_file(temp_file_path, std::ios::binary);
    if (!temp_file) {
        std::cerr << "Failed to create temporary file: " << temp_file_path << std::endl;
        return false;
    }

    // Find the primary key column
    std::string primary_key_column;
    for (const auto& column : schema.columns) {
        if (column.is_primary_key) {
            primary_key_column = column.name;
            break;
        }
    }

    // Read, filter, and write records
    data_file.seekg(0, std::ios::end);
    size_t file_size = data_file.tellg();
    data_file.seekg(0);

    bool found_record = false;
    std::vector<int> deleted_keys; // To track primary keys of deleted records
    std::map<int, int> offset_map; // Maps old positions to new positions

    while (data_file.tellg() < file_size && data_file.good()) {
        int old_offset = data_file.tellg();
        Record record = loadRecord(data_file, schema);

        // Check if this record matches the deletion criteria
        if (record.find(key_column) != record.end() && record[key_column] == key_value) {
            found_record = true;

            // If this is a primary key, store it for index cleanup
            if (primary_key_column != "" && record.find(primary_key_column) != record.end()) {
                deleted_keys.push_back(std::get<int>(record[primary_key_column]));
            }

            // Skip writing this record (effectively deleting it)
            continue;
        }

        // Record doesn't match deletion criteria, write it to the temp file
        int new_offset = temp_file.tellp();
        saveRecord(temp_file, record, schema, new_offset);
        offset_map[old_offset] = new_offset;
    }

    // Close both files
    data_file.close();
    temp_file.close();

    if (!found_record) {
        // No matching record found, delete temp file
        std::filesystem::remove(temp_file_path);
        std::cerr << "No record found matching the criteria" << std::endl;
        return false;
    }

    // Replace original file with temp file
    try {
        std::filesystem::rename(temp_file_path, schema.data_file_path);
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Error replacing file: " << e.what() << std::endl;
        return false;
    }

    // Update index - we need to rebuild it since our B+ tree
    // doesn't support direct deletions
    if (!deleted_keys.empty() && indexes.find(table_name) != indexes.end()) {
        std::string index_file_path = schema.index_file_path + ".tmp";
        BPlusTree* new_index = new BPlusTree(index_file_path);

        // Get all records and rebuild the index
        std::vector<Record> all_records = getAllRecords(table_name);

        for (const auto& record : all_records) {
            if (record.find(primary_key_column) != record.end()) {
                int key = std::get<int>(record.at(primary_key_column));
                // Get the offset for this record from the map
                auto offsets = indexes[table_name]->search(key);
                if (!offsets.empty() && offset_map.find(offsets[0]) != offset_map.end()) {
                    int new_offset = offset_map[offsets[0]];
                    new_index->insert(key, new_offset);
                }
            }
        }

        // Close both indexes
        delete indexes[table_name];
        delete new_index;

        // Replace old index with new one
        try {
            std::filesystem::rename(index_file_path, schema.index_file_path);
            // Reload the index
            indexes[table_name] = new BPlusTree(schema.index_file_path);
        }
        catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "Error replacing index file: " << e.what() << std::endl;
            return false;
        }
    }

    return true;
}

// Helper function for comparing field values
bool compareValues(const FieldValue& left, const FieldValue& right, const std::string& op) {
    // Check that types match
    if (left.index() != right.index()) {
        return false;
    }

    if (op == "=") {
        return left == right;
    }
    else if (op == "!=") {
        return left != right;
    }
    else if (op == ">") {
        if (std::holds_alternative<int>(left)) {
            return std::get<int>(left) > std::get<int>(right);
        }
        else if (std::holds_alternative<float>(left)) {
            return std::get<float>(left) > std::get<float>(right);
        }
        else if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) > std::get<std::string>(right);
        }
    }
    else if (op == "<") {
        if (std::holds_alternative<int>(left)) {
            return std::get<int>(left) < std::get<int>(right);
        }
        else if (std::holds_alternative<float>(left)) {
            return std::get<float>(left) < std::get<float>(right);
        }
        else if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) < std::get<std::string>(right);
        }
    }
    else if (op == ">=") {
        if (std::holds_alternative<int>(left)) {
            return std::get<int>(left) >= std::get<int>(right);
        }
        else if (std::holds_alternative<float>(left)) {
            return std::get<float>(left) >= std::get<float>(right);
        }
        else if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) >= std::get<std::string>(right);
        }
    }
    else if (op == "<=") {
        if (std::holds_alternative<int>(left)) {
            return std::get<int>(left) <= std::get<int>(right);
        }
        else if (std::holds_alternative<float>(left)) {
            return std::get<float>(left) <= std::get<float>(right);
        }
        else if (std::holds_alternative<std::string>(left)) {
            return std::get<std::string>(left) <= std::get<std::string>(right);
        }
    }

    return false;
}

std::vector<Record> DatabaseManager::searchRecordsAdvanced(
    const std::string& table_name,
    const std::vector<std::tuple<std::string, FieldValue, std::string>>& conditions) {

    std::vector<Record> results;

    // Find the table
    TableSchema schema;
    bool found = false;

    for (const auto& table : catalog.tables) {
        if (table.name == table_name) {
            schema = table;
            found = true;
            break;
        }
    }

    if (!found) {
        std::cerr << "Table '" << table_name << "' not found" << std::endl;
        return results;
    }

    // Check if data file exists
    if (!std::filesystem::exists(schema.data_file_path)) {
        std::cerr << "Data file not found: " << schema.data_file_path << std::endl;
        return results;
    }

    // Open data file for reading
    std::ifstream data_file(schema.data_file_path, std::ios::binary);
    if (!data_file) {
        std::cerr << "Failed to open data file: " << schema.data_file_path << std::endl;
        return results;
    }

    // Check if first condition is on primary key and equals operator
    bool useIndex = false;
    std::string primary_key_column;
    for (const auto& column : schema.columns) {
        if (column.is_primary_key) {
            primary_key_column = column.name;
            break;
        }
    }

    // Check if we can use index for the first condition
    if (!conditions.empty() &&
        std::get<0>(conditions[0]) == primary_key_column &&
        std::get<2>(conditions[0]) == "=" &&
        std::holds_alternative<int>(std::get<1>(conditions[0])) &&
        indexes.find(table_name) != indexes.end()) {

        useIndex = true;
    }

    // If we can use the index for the primary key equality
    if (useIndex) {
        int key_value = std::get<int>(std::get<1>(conditions[0]));
        auto offsets = indexes[table_name]->search(key_value);

        for (int offset : offsets) {
            data_file.seekg(offset);
            Record record = loadRecord(data_file, schema);

            // Check all conditions
            bool matches = true;
            for (size_t i = 1; i < conditions.size(); i++) { // Start from 1 since we've used condition 0
                const auto& [col, val, op] = conditions[i];
                if (record.find(col) == record.end() ||
                    !compareValues(record[col], val, op)) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                results.push_back(record);
            }
        }
    }
    else {
        // No index or first condition not on primary key - full table scan
        data_file.seekg(0, std::ios::end);
        size_t file_size = data_file.tellg();
        data_file.seekg(0);

        while (data_file.tellg() < file_size && data_file.good()) {
            Record record = loadRecord(data_file, schema);

            // Check if the record matches all conditions
            bool matches = true;
            for (const auto& [col, val, op] : conditions) {
                if (record.find(col) == record.end() ||
                    !compareValues(record[col], val, op)) {
                    matches = false;
                    break;
                }
            }

            if (matches) {
                results.push_back(record);
            }
        }
    }

    return results;
}