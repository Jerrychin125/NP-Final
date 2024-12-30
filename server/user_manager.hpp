// server/user_manager.hpp
#ifndef USER_MANAGER_HPP
#define USER_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <mutex>

// Structure to hold user information
struct User {
    int fd;                     // File descriptor for the socket
    std::string uname;          // Username
    std::string ucolor;         // Assigned color in hex format (e.g., "#FF5733")
    int cursor_x;               // Cursor X position
    int cursor_y;               // Cursor Y position

    // Parameterized constructor
    User(int fd, const std::string& uname, const std::string& ucolor)
        : fd(fd), uname(uname), ucolor(ucolor), cursor_x(0), cursor_y(0) {}

    // Default constructor
    User() : fd(-1), uname(""), ucolor("#000000"), cursor_x(0), cursor_y(0) {}
};

// Manages online users and their sessions
class UserManager {
public:
    UserManager();

    // Add a new user
    bool addUser(int fd, const std::string& uname, const std::string& ucolor);

    // Remove a user
    bool removeUser(int fd);

    // Check if a username is already taken
    bool isUsernameTaken(const std::string& uname);

    // Get user by file descriptor
    User getUser(int fd);

    // Get all users
    std::unordered_map<int, User> getAllUsers();

private:
    std::unordered_map<int, User> users_; // fd -> User
    std::mutex users_mutex_;
};

#endif // USER_MANAGER_HPP
