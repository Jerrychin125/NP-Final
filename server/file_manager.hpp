// server/file_manager.hpp
#ifndef FILE_MANAGER_HPP
#define FILE_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>
#include "config.hpp"

// Manages file operations and storage
class FileManager {
public:
    FileManager();

    // Load a file from disk
    bool loadFile(const std::string& filename);

    // Save a file to disk
    bool saveFile(const std::string& filename);

    // Apply an edit to a file
    bool applyEdit(const std::string& filename, const std::string& op_type, int x, int y, char new_char);

    // Get file content
    std::vector<std::string> getFileContent(const std::string& filename);

private:
    std::unordered_map<std::string, std::vector<std::string>> files_; // filename -> lines
    std::mutex files_mutex_;
    const std::string storage_path_ = "./data/files/"; // Storage path

    // Helper to read a file from disk
    bool readFromDisk(const std::string& filepath, std::vector<std::string>& content);

    // Helper to write a file to disk
    bool writeToDisk(const std::string& filepath, const std::vector<std::string>& content);
};

#endif // FILE_MANAGER_HPP
