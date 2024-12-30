// server/server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#include <vector>
#include <memory>
#include "file_manager.hpp"
#include "user_manager.hpp"
#include "client_handler.hpp"
#include "config.hpp" // Include config.hpp

// Core server implementation
class Server {
public:
    Server();
    ~Server();

    // Start the server
    bool start();

    // Stop the server
    void stop();

    // Broadcast a message to all clients except exclude_fd
    void broadcastMessage(const Message& msg, int exclude_fd = -1);

    // Remove a client handler
    void removeClient(int client_fd);

private:
    int port_;
    int listen_fd_;
    bool is_running_;

    FileManager file_manager_;
    UserManager user_manager_;

    std::vector<std::unique_ptr<ClientHandler>> clients_;
    std::vector<std::thread> client_threads_;
    std::mutex clients_mutex_;

    // Initialize server socket
    bool initSocket();

    // Accept incoming client connections
    void acceptClients();

};

#endif // SERVER_HPP
