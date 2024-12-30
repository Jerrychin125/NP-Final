// server/config.hpp
#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <string>

// Server configuration constants
const int SERVER_PORT = 8555;                // Server port
const int MAX_CLIENTS = 100;                 // Maximum number of clients
const int BUFFER_SIZE = 4096;                // Buffer size for receiving data
const std::string LOG_FILE = "./logs/server.log"; // Log file path

// Protocol configuration
const std::string DATA_SEPARATOR = "\n";      // Separator for messages

#endif // CONFIG_HPP
