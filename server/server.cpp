// server/server.cpp
#include "server.hpp"
#include "utils.hpp"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h> // For close()
#include <cstring>
#include <arpa/inet.h>
#include <signal.h>

// Constructor
Server::Server()
    : port_(SERVER_PORT), listen_fd_(-1), is_running_(false) {}

// Destructor
Server::~Server() {
    stop();
}

// Initialize server socket
bool Server::initSocket() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        logMessage("Socket creation failed.");
        return false;
    }

    // Set socket options to allow address reuse
    int opt = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logMessage("setsockopt failed.");
        return false;
    }

    // Define server address
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;             // IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;     // Listen on all interfaces
    servaddr.sin_port = htons(port_);          // Server port

    // Bind the socket to the address and port
    if (bind(listen_fd_, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        logMessage("Bind failed.");
        return false;
    }

    // Start listening for incoming connections
    if (listen(listen_fd_, MAX_CLIENTS) < 0) {
        logMessage("Listen failed.");
        return false;
    }

    logMessage("Server initialized on port " + std::to_string(port_) + ".");
    return true;
}

// Start the server
bool Server::start() {
    if (is_running_) {
        logMessage("Server is already running.");
        return false;
    }

    if (!initSocket()) {
        logMessage("Socket initialization failed.");
        return false;
    }

    is_running_ = true;
    logMessage("Server started. Waiting for clients...");

    // Start accepting clients in a separate thread
    std::thread(&Server::acceptClients, this).detach();

    return true;
}

// Stop the server
void Server::stop() {
    if (!is_running_) return;

    is_running_ = false;
    close(listen_fd_);
    logMessage("Server stopped listening for new connections.");

    // Close all client connections
    for (auto& client : clients_) {
        client->sendMessage(Message{
            MessageType::Message,
            json{
                {"packet_type", "message"},
                {"data", {
                    {"message_type", "server_shutdown"},
                    {"message", "Server is shutting down. Disconnecting..."}
                }}
            }
        });
        // ClientHandler destructor will close the socket
    }
    clients_.clear();

    logMessage("All client connections closed.");
}

// Accept incoming client connections
void Server::acceptClients() {
    while (is_running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            if (is_running_) {
                logMessage("Failed to accept client connection.");
            }
            continue;
        }

        // Check if maximum clients reached
        if (user_manager_.getAllUsers().size() >= MAX_CLIENTS) {
            logMessage("Maximum clients reached. Refusing connection from "
                       + std::string(inet_ntoa(client_addr.sin_addr)) + ":"
                       + std::to_string(ntohs(client_addr.sin_port)) + ".");
            close(client_fd);
            continue;
        }

        // Start a new ClientHandler
        auto client_handler = std::make_unique<ClientHandler>(client_fd, file_manager_, user_manager_);
        client_handler->start();
        clients_.emplace_back(std::move(client_handler));

        logMessage("Accepted new client connection (fd: " + std::to_string(client_fd) + ").");
    }
}

// Broadcast a message to all clients except exclude_fd
void Server::broadcastMessage(const Message& msg, int exclude_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& client : clients_) {
        if (client->getClientFD() != exclude_fd) {
            client->sendMessage(msg);
        }
    }
}

// Remove a client handler
void Server::removeClient(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(),
        [client_fd](const std::unique_ptr<ClientHandler>& client) {
            return client->getClientFD() == client_fd;
        }), clients_.end());
    logMessage("Removed client handler for fd: " + std::to_string(client_fd));
}