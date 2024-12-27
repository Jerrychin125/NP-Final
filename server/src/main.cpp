// server/src/main.cpp

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include <atomic> // Added to define std::atomic

// Include nlohmann/json library
#include "json.hpp"

using json = nlohmann::json;

// Constants
const int PORT = 8555;          // Server port
const int MAX_CLIENTS = 100;    // Maximum number of clients
const int BUFFER_SIZE = 4096;   // Buffer size for receiving data

// Struct to represent a User
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

// Global Variables
std::map<int, User> users;                     // Map of file descriptors to Users
std::mutex users_mutex;                        // Mutex to protect the users map
std::vector<std::string> colors = {            // Predefined list of colors
    "#FF5733", "#33FF57", "#3357FF", "#FF33A8",
    "#A833FF", "#33FFF6", "#FF8F33", "#8FFF33",
    "#FF3333", "#33FF8F"
};
int color_index = 0;                           // Index to assign colors
std::mutex color_mutex;                        // Mutex to protect color assignment

std::vector<std::string> shared_buffer = {""}; // Shared document buffer
std::mutex buffer_mutex;                        // Mutex to protect the shared buffer

// Signal Handling for Graceful Shutdown
std::atomic<bool> server_running(true);

void handle_signal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nShutting down server gracefully..." << std::endl;
        server_running = false;
    }
}

// Function to assign a unique color to a new user
std::string assign_color() {
    std::lock_guard<std::mutex> lock(color_mutex);
    std::string color = colors[color_index];
    color_index = (color_index + 1) % colors.size();
    return color;
}

// Function to send all data reliably
bool send_all(int sockfd, const std::string& message) {
    size_t total_sent = 0;
    size_t to_send = message.length();
    const char* data = message.c_str();
    while (total_sent < to_send) {
        ssize_t sent = send(sockfd, data + total_sent, to_send - total_sent, 0);
        if (sent < 0) {
            perror("Send error");
            return false;
        }
        total_sent += sent;
    }
    return true;
}

// Function to broadcast a message to all connected clients
void broadcast_message(const json& message, int exclude_fd = -1) {
    std::lock_guard<std::mutex> lock(users_mutex);
    std::string msg_str = message.dump() + "\n";
    for (const auto& [fd, user] : users) {
        if (fd == exclude_fd) continue; // Skip sending to the sender
        bool success = send_all(fd, msg_str);
        if (!success) {
            std::cerr << "Failed to send message to user: " << user.uname << std::endl;
            // Optionally handle disconnection here
        }
    }
}

// Function to handle individual client connections
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    std::string partial_message = "";

    // Receive and validate username
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[n] = '\0';
    std::string msg_str(buffer, n);
    partial_message += msg_str;

    // Assume the first message is the username in JSON
    size_t pos = partial_message.find('\n');
    if (pos == std::string::npos) {
        // Invalid protocol, no newline found
        close(client_fd);
        return;
    }
    std::string line = partial_message.substr(0, pos);
    partial_message.erase(0, pos + 1);

    try {
        json username_json = json::parse(line);
        if (!username_json.contains("name")) {
            // Invalid message format
            json error_msg = {
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "error_newname_invalid"},
                    {"message", "Invalid username. Name field missing."}
                }}
            };
            send_all(client_fd, error_msg.dump() + "\n");
            close(client_fd);
            return;
        }
        std::string uname = username_json["name"];
        
        // Validate username (e.g., non-empty, allowed characters)
        if (uname.empty()) {
            json error_msg = {
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "error_newname_invalid"},
                    {"message", "Username cannot be empty."}
                }}
            };
            send_all(client_fd, error_msg.dump() + "\n");
            close(client_fd);
            return;
        }

        // Check if username is already taken
        {
            std::lock_guard<std::mutex> lock(users_mutex);
            for (const auto& [fd, user] : users) {
                if (user.uname == uname) {
                    json error_msg = {
                        {"packet_type", "message"},
                        {"data", {
                            {"message_type", "error_newname_taken"},
                            {"message", "Username already taken. Choose another one."}
                        }}
                    };
                    send_all(client_fd, error_msg.dump() + "\n");
                    close(client_fd);
                    return;
                }
            }
        }

        // Assign a unique color to the user
        std::string ucolor = assign_color();

        // Add the user to the users map
        {
            std::lock_guard<std::mutex> lock(users_mutex);
            users.emplace(client_fd, User(client_fd, uname, ucolor));
        }

        std::cout << "User '" << uname << "' connected on socket " << client_fd << "." << std::endl;

        // Send a success message with assigned color and current buffer/collaborators
        json success_msg = {
            {"packet_type", "message"},
            {"data", {
                {"message_type", "connect_success"},
                {"color", ucolor},
                {"buffer", shared_buffer},        // Send current shared buffer
                {"collaborators", json::array()}  // Send current collaborators
            }}
        };
        send_all(client_fd, success_msg.dump() + "\n");

        // Broadcast to other users that a new user has connected
        json user_event = {
            {"packet_type", "user_event"},
            {"data", {
                {"event", "user_connected"},
                {"user", {
                    {"name", uname},
                    {"color", ucolor},
                    {"cursor", { {"x", 0}, {"y", 0} }} // Initial cursor position
                }}
            }}
        };
        broadcast_message(user_event, client_fd);

        // Continuously listen for messages from the client
        while (server_running) { // Corrected from 'while (running)'
            n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (n <= 0) {
                // Client disconnected
                break;
            }
            buffer[n] = '\0';
            std::string recv_str(buffer, n);
            partial_message += recv_str;

            // Process all complete messages
            while ((pos = partial_message.find('\n')) != std::string::npos) {
                std::string message_line = partial_message.substr(0, pos);
                partial_message.erase(0, pos + 1);

                if (message_line.empty()) continue;

                try {
                    json message_json = json::parse(message_line);
                    // Handle different packet types
                    if (message_json["packet_type"] == "operation") {
                        // Handle operation-based updates
                        json data = message_json["data"];
                        std::string op_type = data["type"];
                        int x = data["position"]["x"];
                        int y = data["position"]["y"];
                        std::string character = data["character"];

                        bool valid_operation = false;

                        {
                            std::lock_guard<std::mutex> lock(buffer_mutex);
                            if (op_type == "insert") {
                                if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
                                    shared_buffer[y].insert(shared_buffer[y].begin() + x, character[0]);
                                    valid_operation = true;
                                }
                            }
                            else if (op_type == "delete") {
                                if (y < shared_buffer.size() && x < shared_buffer[y].size()) {
                                    shared_buffer[y].erase(shared_buffer[y].begin() + x);
                                    valid_operation = true;
                                }
                            }
                            else if (op_type == "insert_newline") {
                                if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
                                    std::string new_line = shared_buffer[y].substr(x);
                                    shared_buffer[y] = shared_buffer[y].substr(0, x);
                                    shared_buffer.insert(shared_buffer.begin() + y + 1, new_line);
                                    valid_operation = true;
                                }
                            }
                            else if (op_type == "delete_newline") {
                                if (y < shared_buffer.size() && y > 0) {
                                    int prev_y = y - 1;
                                    users[client_fd].cursor_x = shared_buffer[prev_y].size();
                                    shared_buffer[prev_y] += shared_buffer[y];
                                    shared_buffer.erase(shared_buffer.begin() + y);
                                    valid_operation = true;
                                }
                            }
                        }

                        if (valid_operation) {
                            // Broadcast the operation to other clients
                            broadcast_message(message_json, client_fd);
                            std::cout << "Broadcasted operation '" << op_type << "' from user '" << users[client_fd].uname << "'." << std::endl;
                        }
                        else {
                            std::cerr << "Invalid operation received from user '" << users[client_fd].uname << "'." << std::endl;
                        }
                    }
                    else if (message_json["packet_type"] == "update") {
                        // Handle legacy buffer-based updates if any
                        // Optional: Implement if you still want to support buffer-based updates
                    }
                    // Handle other packet types as needed
                }
                catch (json::parse_error& e) {
                    std::cerr << "JSON parse error: " << e.what() << std::endl;
                    // Optionally send an error message to the client
                }
            }
        }

    } catch (json::parse_error& e) {
        std::cerr << "JSON parse error during username handling: " << e.what() << std::endl;
        close(client_fd);
        return;
    }

    // Client has disconnected
    std::string uname;
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        if (users.find(client_fd) != users.end()) {
            uname = users[client_fd].uname;
            users.erase(client_fd);
        }
    }

    std::cout << "User '" << uname << "' disconnected." << std::endl;

    // Broadcast to other users that a user has disconnected
    json disconnect_event = {
        {"packet_type", "user_event"},
        {"data", {
            {"event", "user_disconnected"},
            {"user", {
                {"name", uname}
            }}
        }}
    };
    broadcast_message(disconnect_event, client_fd);

    close(client_fd);
}

// Function to start the server and listen for incoming connections
int main() {
    // Register signal handler for graceful shutdown
    signal(SIGINT, handle_signal);

    // Create a TCP socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("setsockopt failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Define server address
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;             // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;     // Listen on all interfaces
    servaddr.sin_port = htons(PORT);           // Server port

    // Bind the socket to the address and port
    if (bind(listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("Bind failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(listen_fd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    std::cout << "Server started on port " << PORT << "." << std::endl;

    // Start a thread to receive messages (optional, depending on design)
    // For this implementation, we'll handle clients in separate threads.

    // Main loop to accept incoming connections
    while (server_running) { // Use the atomic flag for loop control
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (server_running) { // Only report errors if the server is still running
                perror("Accept failed");
            }
            continue; // Continue accepting other connections
        }

        // Check if maximum clients reached
        {
            std::lock_guard<std::mutex> lock(users_mutex);
            if (users.size() >= MAX_CLIENTS) {
                std::cerr << "Maximum clients reached. Refusing connection from "
                          << inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port) << "." << std::endl;
                close(client_fd);
                continue;
            }
        }

        // Start a new thread to handle the client
        std::thread client_thread(handle_client, client_fd);
        client_thread.detach(); // Detach the thread to allow independent execution
    }

    // Close the listening socket
    close(listen_fd);

    // Optionally, notify all clients about server shutdown
    json shutdown_msg = {
        {"packet_type", "message"},
        {"data", {
            {"message_type", "server_shutdown"},
            {"message", "Server is shutting down. Disconnecting..."}
        }}
    };
    broadcast_message(shutdown_msg);

    // Close all client connections
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        for (const auto& [fd, user] : users) {
            send_all(fd, shutdown_msg.dump() + "\n");
            close(fd);
        }
        users.clear();
    }

    std::cout << "Server shutdown complete." << std::endl;

    return 0;
}
