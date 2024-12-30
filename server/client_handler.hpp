// server/client_handler.hpp
#ifndef CLIENT_HANDLER_HPP
#define CLIENT_HANDLER_HPP

#include <thread>
#include "file_manager.hpp"
#include "user_manager.hpp"
#include "protocol.hpp"
#include "config.hpp"

// Handles communication with a single client
class ClientHandler {
public:
    ClientHandler(int client_fd, FileManager& file_manager, UserManager& user_manager);
    ~ClientHandler();
    int client_fd_;

    // Start the client handler thread
    void start();

    // Join the client handler thread
    void join();

    // Get client file descriptor
    int getClientFD() const;

    // Send a message to the client
    bool sendMessage(const Message& msg);


    void handleClient();
    

private:
    // int client_fd_;
    FileManager& file_manager_;
    UserManager& user_manager_;
    std::thread thread_;
    bool is_running_;

    // Process a received message
    void processMessage(const Message& msg);

    // Close client connection
    void closeConnection();
};

#endif // CLIENT_HANDLER_HPP
