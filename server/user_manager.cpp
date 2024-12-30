// server/user_manager.cpp
#include "user_manager.hpp"
#include "utils.hpp"

UserManager::UserManager() {}

bool UserManager::addUser(int fd, const std::string& uname, const std::string& ucolor) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    if (users_.find(fd) != users_.end()) {
        logMessage("Attempted to add user with existing fd: " + std::to_string(fd));
        return false;
    }
    // Check if username is already taken
    for (const auto& [key, user] : users_) {
        if (user.uname == uname) {
            logMessage("Username already taken: " + uname);
            return false;
        }
    }
    users_.emplace(fd, User(fd, uname, ucolor));
    logMessage("Added user: " + uname + " with fd: " + std::to_string(fd));
    return true;
}

bool UserManager::removeUser(int fd) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    auto it = users_.find(fd);
    if (it != users_.end()) {
        logMessage("Removing user: " + it->second.uname + " with fd: " + std::to_string(fd));
        users_.erase(it);
        return true;
    }
    logMessage("Attempted to remove non-existent user with fd: " + std::to_string(fd));
    return false;
}

bool UserManager::isUsernameTaken(const std::string& uname) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    for (const auto& [fd, user] : users_) {
        if (user.uname == uname) {
            return true;
        }
    }
    return false;
}

User UserManager::getUser(int fd) {
    std::lock_guard<std::mutex> lock(users_mutex_);
    if (users_.find(fd) != users_.end()) {
        return users_[fd];
    }
    return User();
}

std::unordered_map<int, User> UserManager::getAllUsers() {
    std::lock_guard<std::mutex> lock(users_mutex_);
    return users_;
}
