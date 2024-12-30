// server/main.cpp
#include "server.hpp"
#include "utils.hpp"
#include <csignal>
#include <atomic>

// Global atomic flag for graceful shutdown
std::atomic<bool> running(true);

// Signal handler for graceful shutdown
void signalHandler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        logMessage("Interrupt signal (" + std::to_string(signal) + ") received.");
        running = false;
    }
}

int main() {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Initialize and start the server
    Server server;
    if (!server.start()) {
        logMessage("Failed to start the server.");
        return -1;
    }

    // Keep the main thread alive until a shutdown signal is received
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Stop the server gracefully
    server.stop();
    logMessage("Server shutdown gracefully.");

    return 0;
}
