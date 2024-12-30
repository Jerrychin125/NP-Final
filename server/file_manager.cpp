// server/file_manager.cpp
#include "file_manager.hpp"
#include "protocol.hpp"
#include "utils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

// Constructor
FileManager::FileManager() {
    // Ensure the storage path exists
    if (!std::filesystem::exists(storage_path_)) {
        std::filesystem::create_directories(storage_path_);
        logMessage("Created storage directory: " + storage_path_);
    }
}

bool FileManager::loadFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    std::string filepath = storage_path_ + filename;
    std::vector<std::string> content;
    if (readFromDisk(filepath, content)) {
        files_[filename] = content;
        logMessage("Loaded file: " + filename);
        return true;
    }
    logMessage("Failed to load file: " + filename);
    return false;
}

bool FileManager::saveFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto it = files_.find(filename);
    if (it == files_.end()) {
        logMessage("File not found in memory: " + filename);
        return false;
    }
    std::string filepath = storage_path_ + filename;
    if (writeToDisk(filepath, it->second)) {
        logMessage("Saved file: " + filename);
        return true;
    }
    logMessage("Failed to save file: " + filename);
    return false;
}

bool FileManager::applyEdit(const std::string& filename, const std::string& op_type, int x, int y, char new_char) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto it = files_.find(filename);
    if (it == files_.end()) {
        logMessage("File not loaded: " + filename);
        return false;
    }

    std::vector<std::string>& content = it->second;

    if (y < 0 || y >= static_cast<int>(content.size())) {
        logMessage("Invalid line number: " + std::to_string(y));
        return false;
    }

    std::string& line = content[y];

    switch (getOperationType(op_type)) {
        case OperationType::Insert:
            if (x < 0 || x > static_cast<int>(line.size())) {
                logMessage("Invalid column number for insert: " + std::to_string(x));
                return false;
            }
            line.insert(line.begin() + x, new_char);
            logMessage("Inserted character '" + std::string(1, new_char) + "' at (" + std::to_string(x) + ", " + std::to_string(y) + ") in file " + filename);
            break;

        case OperationType::Delete:
            if (x < 0 || x >= static_cast<int>(line.size())) {
                logMessage("Invalid column number for delete: " + std::to_string(x));
                return false;
            }
            line.erase(line.begin() + x);
            logMessage("Deleted character at (" + std::to_string(x) + ", " + std::to_string(y) + ") in file " + filename);
            break;

        case OperationType::InsertNewline:
            if (x < 0 || x > static_cast<int>(line.size())) {
                logMessage("Invalid column number for insert_newline: " + std::to_string(x));
                return false;
            }
            {
                std::string new_line = line.substr(x);
                line = line.substr(0, x);
                content.insert(content.begin() + y + 1, new_line);
                logMessage("Inserted newline at (" + std::to_string(x) + ", " + std::to_string(y) + ") in file " + filename);
            }
            break;

        case OperationType::DeleteNewline:
            if (y <= 0 || y >= static_cast<int>(content.size())) {
                logMessage("Invalid line number for delete_newline: " + std::to_string(y));
                return false;
            }
            {
                std::string& prev_line = content[y - 1];
                prev_line += content[y];
                content.erase(content.begin() + y);
                logMessage("Deleted newline at line " + std::to_string(y) + " in file " + filename);
            }
            break;

        default:
            logMessage("Unknown operation type: " + op_type);
            return false;
    }

    return true;
}

std::vector<std::string> FileManager::getFileContent(const std::string& filename) {
    std::lock_guard<std::mutex> lock(files_mutex_);
    auto it = files_.find(filename);
    if (it != files_.end()) {
        return it->second;
    }
    return {};
}

bool FileManager::readFromDisk(const std::string& filepath, std::vector<std::string>& content) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        logMessage("Unable to open file for reading: " + filepath);
        return false;
    }
    std::string line;
    while (std::getline(file, line)) {
        content.push_back(line);
    }
    file.close();
    return true;
}

bool FileManager::writeToDisk(const std::string& filepath, const std::vector<std::string>& content) {
    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open()) {
        logMessage("Unable to open file for writing: " + filepath);
        return false;
    }
    for (const auto& line : content) {
        file << line << "\n";
    }
    file.close();
    return true;
}
