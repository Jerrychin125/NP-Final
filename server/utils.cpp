// server/utils.cpp
#include "utils.hpp"
#include "config.hpp"
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <mutex>

std::mutex log_mutex;

void logMessage(const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    std::ofstream log_file;
    log_file.open(LOG_FILE, std::ios::app);
    if (log_file.is_open()) {
        log_file << "[" << getCurrentTimestamp() << "] " << message << std::endl;
        log_file.close();
    } else {
        std::cerr << "Unable to open log file: " << LOG_FILE << std::endl;
    }
}

std::string getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    // Remove the newline character from ctime output
    std::string time_str = std::ctime(&now_time);
    if (!time_str.empty() && time_str[time_str.length() - 1] == '\n') {
        time_str.erase(time_str.length() - 1);
    }
    return time_str;
}
