// server/protocol.hpp
#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <string>
#include "json.hpp" // Requires nlohmann/json library

using json = nlohmann::json;

// Define message types
enum class MessageType {
    Operation,
    UserEvent,
    Message,
    Unknown
};

// Define operation types
enum class OperationType {
    Insert,
    Delete,
    InsertNewline,
    DeleteNewline,
    Unknown
};

// Structure for messages
struct Message {
    MessageType type;
    json data;
};

// Serialization: Convert Message struct to JSON string
std::string serializeMessage(const Message& msg);

// Deserialization: Convert JSON string to Message struct
Message deserializeMessage(const std::string& raw_data);

// Function to convert string to OperationType enum
OperationType getOperationType(const std::string& op_type);

#endif // PROTOCOL_HPP
