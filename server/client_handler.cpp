// server/client_handler.cpp
#include "client_handler.hpp"
#include "utils.hpp"
#include <sys/socket.h>
#include <unistd.h> // For close()
#include <cstring>

// Constructor
ClientHandler::ClientHandler(int client_fd, FileManager& file_manager, UserManager& user_manager)
    : client_fd_(client_fd), file_manager_(file_manager), user_manager_(user_manager), is_running_(false) {}

// Destructor
ClientHandler::~ClientHandler() {
    closeConnection();
}

// Start the client handler thread
void ClientHandler::start() {
    is_running_ = true;
    thread_ = std::thread(&ClientHandler::handleClient, this);
}

// Join the client handler thread
void ClientHandler::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

// Get client file descriptor
int ClientHandler::getClientFD() const {
    return client_fd_;
}

// Main loop for handling client communication
void ClientHandler::handleClient() {
    logMessage("Handling client_fd: " + std::to_string(client_fd_));
    char buffer[BUFFER_SIZE];
    std::string partial_message = "";

    // Receive and validate username
    ssize_t n = recv(client_fd_, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        logMessage("Failed to receive username from client_fd: " + std::to_string(client_fd_));
        closeConnection();
        return;
    }
    buffer[n] = '\0';
    std::string msg_str(buffer, n);
    partial_message += msg_str;
    logMessage("Received message from client_fd " + std::to_string(client_fd_) + ": " + msg_str);

    // Assume the first message is the username in JSON
    size_t pos = partial_message.find('\n');
    if (pos == std::string::npos) {
        // Invalid protocol, no newline found
        logMessage("Invalid protocol from client_fd: " + std::to_string(client_fd_));
        closeConnection();
        return;
    }
    std::string line = partial_message.substr(0, pos);
    partial_message.erase(0, pos + 1);

    try {
        Message username_msg = deserializeMessage(line);
        // logMessage("Deserialized message from client_fd " + std::to_string(client_fd_) + ": Type=" + std::to_string(static_cast<int>(username_msg.type)) + ", Data=" + username_msg.data.dump());
        if (username_msg.type != MessageType::Message) {
            // Invalid message type
            json error_msg = {
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "error_newname_invalid"},
                    {"message", "Invalid message type. Expected 'message'."}
                }}
            };
            sendMessage(Message{MessageType::Message, error_msg["data"]});
            closeConnection();
            return;
        }

        std::string uname = username_msg.data.value("name", "");
        if (uname.empty()) {
            json error_msg = {
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "error_newname_invalid"},
                    {"message", "Username cannot be empty."}
                }}
            };
            sendMessage(Message{MessageType::Message, error_msg["data"]});
            closeConnection();
            return;
        }

        // Check if username is already taken
        if (user_manager_.isUsernameTaken(uname)) {
            json error_msg = {
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "error_newname_taken"},
                    {"message", "Username already taken. Choose another one."}
                }}
            };
            sendMessage(Message{MessageType::Message, error_msg["data"]});
            closeConnection();
            return;
        }


        // Assign a unique color to the user
        // For simplicity, reuse the assign_color function or integrate into UserManager
        // Here, we assume UserManager handles color assignment

        // Add the user to the UserManager
        // Assuming UserManager has a method to assign color
        // For now, generate a random color or use a predefined list
        // Here, we'll use a simple color assignment
        std::vector<std::string> colors = {
            "#FF5733", "#33FF57", "#3357FF", "#FF33A8",
            "#A833FF", "#33FFF6", "#FF8F33", "#8FFF33",
            "#FF3333", "#33FF8F"
        };
        static int color_index = 0;
        std::string ucolor = colors[color_index % colors.size()];
        color_index++;


        user_manager_.addUser(client_fd_, uname, ucolor);

        logMessage("User '" + uname + "' connected on fd " + std::to_string(client_fd_) + ".");

        // Send a success message with assigned color and current buffer
        json success_msg = {
            {"packet_type", "message"},
            {"data", {
                {"message_type", "connect_success"},
                {"color", ucolor},
                {"buffer", file_manager_.getFileContent("shared_document.txt")}, // Assuming a default file
                {"collaborators", json::array()} // To be filled with current users
            }}
        };

        // Populate collaborators
        auto all_users = user_manager_.getAllUsers();
        for (const auto& [fd, user] : all_users) {
            if (fd == client_fd_) continue;
            success_msg["data"]["collaborators"].push_back({
                {"name", user.uname},
                {"color", user.ucolor},
                {"cursor", { {"x", user.cursor_x}, {"y", user.cursor_y} }}
            });
        }

        sendMessage(Message{MessageType::Message, success_msg["data"]});

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
        // Broadcast excluding the current client
        Message broadcast_msg{MessageType::UserEvent, user_event["data"]};
        // Assuming Server has a method to broadcast, else use a global or pass a reference
        // Here, for simplicity, we'll assume broadcast_message is accessible
        // You might need to adjust this based on your actual Server implementation
        // For now, we will leave it as a placeholder
        // broadcast_message(broadcast_msg, client_fd_);

        // Continuously listen for messages from the client

        while (is_running_) {
            n = recv(client_fd_, buffer, sizeof(buffer) - 1, 0);
            logMessage("333333");

            // logMessage("typing");
            if (n <= 0) {
                if (n == -1) {
                    logMessage("recv error on client_fd " + std::to_string(client_fd_) + ": " + std::strerror(errno));
                    break;
                }
                else if (n == 0) {
                    logMessage("Client_fd " + std::to_string(client_fd_) + " has closed the connection.");
                    break;
                }
                break;
            }
            logMessage("222222");
            buffer[n] = '\0';
            std::string recv_str(buffer, n);
            partial_message += recv_str;
            // Process all complete messages
            while ((pos = partial_message.find(DATA_SEPARATOR)) != std::string::npos) {
                std::string message_line = partial_message.substr(0, pos);
                partial_message.erase(0, pos + DATA_SEPARATOR.length());

                if (message_line.empty()) continue;

                Message msg = deserializeMessage(message_line);
                processMessage(msg);
            }
        }

    } catch (json::parse_error& e) {
        logMessage(std::string("JSON parse error during username handling: ") + e.what());
        closeConnection();
        return;
    }

    // Client has disconnected
    std::string uname;
    user_manager_.removeUser(client_fd_);
    {
        // Get username before removal
        // Assuming UserManager provides a method to get username
        // Here, you might need to adjust based on your actual implementation
        // For simplicity, we leave it as a placeholder
        // uname = user_manager_.getUser(client_fd_).uname;
    }

    logMessage("User disconnected: " + uname + " on fd " + std::to_string(client_fd_) + ".");

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
    Message disconnect_msg{MessageType::UserEvent, disconnect_event["data"]};
    // broadcast_message(disconnect_msg, client_fd_);

    closeConnection();
}

void ClientHandler::processMessage(const Message& msg) {
    switch (msg.type) {
        case MessageType::Operation:
            {
                // Handle operation-based updates
                std::string op_type = msg.data.value("type", "unknown");
                int x = msg.data["position"].value("x", 0);
                int y = msg.data["position"].value("y", 0);
                std::string character = msg.data.value("character", "");
                char new_char = character.empty() ? '\0' : character[0];

                std::string filename = msg.data.value("filename", "shared_document.txt"); // Default file
                logMessage("Processing operation from client_fd " + std::to_string(client_fd_) + ": Type=" + op_type + ", Position=(" + std::to_string(x) + "," + std::to_string(y) + "), Character='" + character + "'");

                bool valid_operation = file_manager_.applyEdit(filename, op_type, x, y, new_char);

                if (valid_operation) {
                    // Broadcast the operation to other clients
                    // Placeholder: Implement broadcasting logic
                    // broadcastMessage(msg, client_fd_);
                    logMessage("Broadcasted operation '" + op_type + "' from user fd " + std::to_string(client_fd_) + ".");
                }
                else {
                    logMessage("Invalid operation received from user fd " + std::to_string(client_fd_) + ".");
                    // Optionally, send an error message to the client
                }
            }
            break;

        case MessageType::UserEvent:
            {
                // Handle user events like cursor updates
                // Placeholder: Implement as needed
            }
            break;

        case MessageType::Message:
            {
                // Handle generic messages
                // Placeholder: Implement as needed
            }
            break;

        default:
            logMessage("Received unknown message type from fd " + std::to_string(client_fd_) + ".");
            break;
    }
}

bool ClientHandler::sendMessage(const Message& msg) {
    std::string msg_str = serializeMessage(msg);
    size_t total_sent = 0;
    size_t to_send = msg_str.length();
    const char* data = msg_str.c_str();
    while (total_sent < to_send) {
        ssize_t sent = send(client_fd_, data + total_sent, to_send - total_sent, 0);
        if (sent < 0) {
            perror("Send error");
            return false;
        }
        total_sent += sent;
    }
    return true;
}

void ClientHandler::closeConnection() {
    if (client_fd_ != -1) {
        close(client_fd_);
        client_fd_ = -1;
        is_running_ = false;
        logMessage("Closed connection for client_fd.");
    }
}
