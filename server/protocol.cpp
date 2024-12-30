// server/protocol.cpp
#include "protocol.hpp"
#include "config.hpp"
#include "utils.hpp"

std::string serializeMessage(const Message& msg) {
    json root;
    switch (msg.type) {
        case MessageType::Operation:
            root["packet_type"] = "operation";
            root["data"] = msg.data;
            break;
        case MessageType::UserEvent:
            root["packet_type"] = "user_event";
            root["data"] = msg.data;
            break;
        case MessageType::Message:
            root["packet_type"] = "message";
            root["data"] = msg.data;
            break;
        default:
            root["packet_type"] = "unknown";
            break;
    }
    return root.dump() + DATA_SEPARATOR;
}

Message deserializeMessage(const std::string& raw_data) {
    Message msg;
    try {
        json root = json::parse(raw_data);
        std::string packet_type = root.value("packet_type", "unknown");
        if (packet_type == "operation") {
            msg.type = MessageType::Operation;
        }
        else if (packet_type == "user_event") {
            msg.type = MessageType::UserEvent;
        }
        else if (packet_type == "message") {
            msg.type = MessageType::Message;
        }
        else {
            msg.type = MessageType::Unknown;
        }
        msg.data = root.value("data", json::object());
    }
    catch (json::parse_error& e) {
        logMessage(std::string("JSON parse error: ") + e.what());
        msg.type = MessageType::Unknown;
    }
    return msg;
}

// Function to convert string to OperationType enum
OperationType getOperationType(const std::string& op_type) {
    if (op_type == "insert") {
        return OperationType::Insert;
    }
    else if (op_type == "delete") {
        return OperationType::Delete;
    }
    else if (op_type == "insert_newline") {
        return OperationType::InsertNewline;
    }
    else if (op_type == "delete_newline") {
        return OperationType::DeleteNewline;
    }
    else {
        return OperationType::Unknown;
    }
}
