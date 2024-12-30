// client/src/main.cpp

#include <SFML/Graphics.hpp>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <atomic>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;

// Struct to represent a collaborator
struct Collaborator {
    std::string name;
    int cursor_x;
    int cursor_y;
    sf::Color color;
};

// Global variables
std::vector<std::string> shared_buffer = {""};
std::mutex buffer_mutex;

std::map<std::string, Collaborator> collaborators; // name -> Collaborator
std::mutex collaborators_mutex;

std::string user_name;
sf::Color user_color = sf::Color::Black;

std::atomic<bool> running(true);

// Networking variables
int sockfd;

// Function to convert hex color string to SFML Color
sf::Color hex_to_color(const std::string& hex) {
    unsigned int r, g, b;
    sscanf(hex.c_str(), "#%02x%02x%02x", &r, &g, &b);
    return sf::Color(r, g, b);
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

// Function to send JSON messages over the socket
bool send_json(const json& message) {
    std::string msg_str = message.dump() + "\n";
    if (!send_all(sockfd, msg_str)) {
        std::cerr << "Failed to send message to server." << std::endl;
        return false;
    }
    return true;
}

// Function to handle incoming messages from the server
void receive_messages() {
    char buffer[4096];
    std::string recv_buffer = "";
    while (running) {
        ssize_t n = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            recv_buffer += buffer;
            size_t pos = 0;
            while ((pos = recv_buffer.find('\n')) != std::string::npos) {
                std::string line = recv_buffer.substr(0, pos);
                recv_buffer.erase(0, pos + 1);
                if (line.empty()) continue;
                try {
                    json message = json::parse(line);
                    if (message["packet_type"] == "message") {
                        std::string msg_type = message["data"]["message_type"];
                        if (msg_type == "connect_success") {
                            // Receive initial buffer and collaborators
                            {
                                std::lock_guard<std::mutex> lock(buffer_mutex);
                                shared_buffer = message["data"]["buffer"].get<std::vector<std::string>>();
                            }
                            // Receive collaborators
                            json collabs = message["data"]["collaborators"];
                            std::lock_guard<std::mutex> lock(collaborators_mutex);
                            for (const auto& collab : collabs) {
                                Collaborator c;
                                c.name = collab["name"];
                                c.color = hex_to_color(collab["color"].get<std::string>());
                                c.cursor_x = collab["cursor"]["x"];
                                c.cursor_y = collab["cursor"]["y"];
                                collaborators[c.name] = c;
                            }
                            // Update user's color if provided
                            if (message["data"].contains("color")) {
                                user_color = hex_to_color(message["data"]["color"].get<std::string>());
                            }
                            std::cout << "Connected to server successfully." << std::endl;
                        }
                        else if (msg_type == "error_newname_invalid" || msg_type == "error_newname_taken") {
                            std::cout << "Error: " << message["data"]["message"] << std::endl;
                            running = false;
                            break;
                        }
                        else {
                            // Handle other message types if needed
                        }
                    }
                    else if (message["packet_type"] == "user_event") {
                        std::string event = message["data"]["event"];
                        if (event == "user_connected") {
                            json user = message["data"]["user"];
                            Collaborator c;
                            c.name = user["name"];
                            c.color = hex_to_color(user["color"].get<std::string>());
                            c.cursor_x = user["cursor"]["x"];
                            c.cursor_y = user["cursor"]["y"];
                            std::lock_guard<std::mutex> lock(collaborators_mutex);
                            collaborators[c.name] = c;
                            std::cout << "User '" << c.name << "' connected." << std::endl;
                        }
                        else if (event == "user_disconnected") {
                            std::string name = message["data"]["user"]["name"];
                            std::lock_guard<std::mutex> lock(collaborators_mutex);
                            collaborators.erase(name);
                            std::cout << "User '" << name << "' disconnected." << std::endl;
                        }
                    }
                    else if (message["packet_type"] == "operation") {
                        // Handle incoming operation
                        json data = message["data"];
                        std::string op_type = data["type"];
                        int x = data["position"]["x"];
                        int y = data["position"]["y"];
                        std::string character = data["character"];

                        std::lock_guard<std::mutex> lock(buffer_mutex);
                        if (op_type == "insert") {
                            if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
                                shared_buffer[y].insert(shared_buffer[y].begin() + x, character[0]);
                            }
                        }
                        else if (op_type == "delete") {
                            if (y < shared_buffer.size() && x < shared_buffer[y].size()) {
                                shared_buffer[y].erase(shared_buffer[y].begin() + x);
                            }
                        }
                        else if (op_type == "insert_newline") {
                            if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
                                std::string new_line = shared_buffer[y].substr(x);
                                shared_buffer[y] = shared_buffer[y].substr(0, x);
                                shared_buffer.insert(shared_buffer.begin() + y + 1, new_line);
                            }
                        }
                        else if (op_type == "delete_newline") {
                            if (y < shared_buffer.size() && y > 0) {
                                int prev_y = y - 1;
                                shared_buffer[prev_y] += shared_buffer[y];
                                shared_buffer.erase(shared_buffer.begin() + y);
                            }
                        }

                        std::cout << "Applied operation '" << op_type << "' from server." << std::endl;
                    }
                    else if (message["packet_type"] == "update") {
                        // Handle cursor position updates from other users
                        std::string user_name = message["data"]["name"];
                        int cursor_x = message["data"]["cursor"]["x"];
                        int cursor_y = message["data"]["cursor"]["y"];

                        std::lock_guard<std::mutex> lock(collaborators_mutex);
                        if (collaborators.find(user_name) != collaborators.end()) {
                            collaborators[user_name].cursor_x = cursor_x;
                            collaborators[user_name].cursor_y = cursor_y;
                        }
                        else {
                            // Optionally handle cases where the user is not in the collaborators map
                            // For example, add the user to the map
                            Collaborator new_collab;
                            new_collab.name = user_name;
                            new_collab.color = sf::Color::White; // Default color or handle appropriately
                            new_collab.cursor_x = cursor_x;
                            new_collab.cursor_y = cursor_y;
                            collaborators[user_name] = new_collab;
                        }
                    }
                }
                catch (json::parse_error& e) {
                    std::cerr << "JSON parse error: " << e.what() << std::endl;
                }
            }
        }
        else if (n == 0) {
            std::cout << "Server closed the connection." << std::endl;
            running = false;
            return;
        }
        else {
            if (errno == EINTR)
                continue;
            perror("Recv error");
            running = false;
            return;
        }
    }
}

// Function to apply an operation to the local buffer
void apply_operation(const json& operation) {
    std::string op_type = operation["type"];
    int x = operation["position"]["x"];
    int y = operation["position"]["y"];
    std::string character = operation["character"];

    std::lock_guard<std::mutex> lock(buffer_mutex);
    if (op_type == "insert") {
        if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
            shared_buffer[y].insert(shared_buffer[y].begin() + x, character[0]);
        }
    }
    else if (op_type == "delete") {
        if (y < shared_buffer.size() && x < shared_buffer[y].size()) {
            shared_buffer[y].erase(shared_buffer[y].begin() + x);
        }
    }
    else if (op_type == "insert_newline") {
        if (y < shared_buffer.size() && x <= shared_buffer[y].size()) {
            std::string new_line = shared_buffer[y].substr(x);
            shared_buffer[y] = shared_buffer[y].substr(0, x);
            shared_buffer.insert(shared_buffer.begin() + y + 1, new_line);
        }
    }
    else if (op_type == "delete_newline") {
        if (y < shared_buffer.size() && y > 0) {
            int prev_y = y - 1;
            shared_buffer[prev_y] += shared_buffer[y];
            shared_buffer.erase(shared_buffer.begin() + y);
        }
    }
}

int main() {
    // Load font
    sf::Font font;
    if (!font.openFromFile("resources/fonts/Cascadia.ttf")) {
        std::cerr << "Failed to load Cascadia.ttf. Ensure the font file exists in 'resources/fonts/'." << std::endl;
        return -1;
    }

    // Setup text display
    sf::Text text_display(font);
    text_display.setFont(font);
    text_display.setCharacterSize(16);
    text_display.setFillColor(sf::Color::Black);
    text_display.setPosition(sf::Vector2f(10.f, 10.f));

    // Setup own cursor rectangle
    sf::RectangleShape cursor_rect(sf::Vector2f(2, 20));
    cursor_rect.setFillColor(sf::Color::Black);

    // Prompt for server IP and port
    std::string server_ip = "127.0.0.1"; // Default
    unsigned short server_port = 8555;    // Default

    std::cout << "Enter server IP [127.0.0.1]: ";
    std::string input_ip;
    std::getline(std::cin, input_ip);
    if (!input_ip.empty()) server_ip = input_ip;

    std::cout << "Enter server port [8555]: ";
    std::string input_port;
    std::getline(std::cin, input_port);
    if (!input_port.empty()) server_port = static_cast<unsigned short>(std::stoi(input_port));

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("Socket creation failed");
        return -1;
    }

    // Server address
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &servaddr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        perror("Connection Failed");
        close(sockfd);
        return -1;
    }

    // Start a thread to receive messages
    std::thread recv_thread(receive_messages);
    recv_thread.detach();

    // Prompt for username
    std::cout << "Enter your username: ";
    std::cin >> user_name;
    std::cin.ignore(); // Ignore remaining newline

    // Send username as JSON
    json username_msg = { {"name", user_name} };
    if (!send_json(username_msg)) {
        std::cerr << "Failed to send username to server." << std::endl;
        running = false;
    }

    // Initialize SFML window
    sf::VideoMode vm({800, 600}, 32);
    sf::RenderWindow window(vm, user_name);
    window.setFramerateLimit(60);

    // Main loop variables
    int cursor_x = 0;
    int cursor_y = 0;

    while (window.isOpen() && running) {
        
        while (const std::optional event = window.pollEvent()) {
            if (event->is<sf::Event::Closed>()) {
                window.close();
                running = false;
                break;
            }

            // Handle text input
            if (const auto* textEntered = event->getIf<sf::Event::TextEntered>()) {
                char32_t unicode = textEntered->unicode;
                if (unicode == '\b') { // Backspace
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    if (!shared_buffer.empty()) {
                        if (cursor_x > 0) {
                            cursor_x--;
                            char deleted_char = shared_buffer[cursor_y].at(cursor_x);
                            shared_buffer[cursor_y].erase(cursor_x, 1);

                            // Send delete operation
                            json delete_op = {
                                {"packet_type", "operation"},
                                {"data", {
                                    {"type", "delete"},
                                    {"position", { {"x", cursor_x}, {"y", cursor_y} }},
                                    {"character", std::string(1, deleted_char)}
                                }}
                            };
                            send_json(delete_op);
                        }
                        else if (cursor_y > 0) {
                            // Send delete_newline operation
                            json delete_newline_op = {
                                {"packet_type", "operation"},
                                {"data", {
                                    {"type", "delete_newline"},
                                    {"position", { {"x", 0}, {"y", cursor_y} }},
                                    {"character", ""}
                                }}
                            };
                            send_json(delete_newline_op);

                            // Merge lines locally
                            cursor_x = shared_buffer[cursor_y - 1].size();
                            shared_buffer[cursor_y - 1] += shared_buffer[cursor_y];
                            shared_buffer.erase(shared_buffer.begin() + cursor_y);
                            cursor_y--;
                        }
                    }
                }
                else if (unicode == '\r' || unicode == '\n') { // Enter key
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    // Send insert_newline operation
                    json insert_newline_op = {
                        {"packet_type", "operation"},
                        {"data", {
                            {"type", "insert_newline"},
                            {"position", { {"x", cursor_x}, {"y", cursor_y} }},
                            {"character", "\n"}
                        }}
                    };
                    send_json(insert_newline_op);

                    // Insert newline locally
                    std::string new_line = shared_buffer[cursor_y].substr(cursor_x);
                    shared_buffer[cursor_y] = shared_buffer[cursor_y].substr(0, cursor_x);
                    shared_buffer.insert(shared_buffer.begin() + cursor_y + 1, new_line);
                    cursor_y++;
                    cursor_x = 0;
                }
                else if (unicode >= 32 && unicode <= 126) { // Printable characters
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    if (cursor_y < shared_buffer.size()) {
                        char inserted_char = static_cast<char>(unicode);
                        shared_buffer[cursor_y].insert(shared_buffer[cursor_y].begin() + cursor_x, inserted_char);
                        // Send insert operation
                        json insert_op = {
                            {"packet_type", "operation"},
                            {"data", {
                                {"type", "insert"},
                                {"position", { {"x", cursor_x}, {"y", cursor_y} }},
                                {"character", std::string(1, inserted_char)}
                            }}
                        };
                        send_json(insert_op);
                        cursor_x++;
                    }
                }
                json cursor_msg = {
                    {"packet_type", "update"},
                    {"data", {
                        {"cursor", { {"x", cursor_x}, {"y", cursor_y} }}
                    }}
                };
                send_json(cursor_msg);
            }

            // Handle key presses for navigation
            if (const auto* keyEvent = event->getIf<sf::Event::KeyPressed>()) {
                bool moved = false;
                std::lock_guard<std::mutex> lock(buffer_mutex);
                sf::Keyboard::Key key = keyEvent->code;

                if (key == sf::Keyboard::Key::Left) {
                    if (cursor_x > 0) {
                        cursor_x--;
                        moved = true;
                    }
                    else if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = shared_buffer[cursor_y].size();
                        moved = true;
                    }
                }
                if (key == sf::Keyboard::Key::Right) {
                    if (cursor_y < shared_buffer.size()) {
                        if (cursor_x < shared_buffer[cursor_y].size()) {
                            cursor_x++;
                            moved = true;
                        }
                        else if (cursor_y < shared_buffer.size() - 1) {
                            cursor_y++;
                            cursor_x = 0;
                            moved = true;
                        }
                    }
                }
                if (key == sf::Keyboard::Key::Up) {
                    if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = std::min(cursor_x, static_cast<int>(shared_buffer[cursor_y].size()));
                        moved = true;
                    }
                }
                if (key == sf::Keyboard::Key::Down) {
                    if (cursor_y < shared_buffer.size() - 1) {
                        cursor_y++;
                        cursor_x = std::min(cursor_x, static_cast<int>(shared_buffer[cursor_y].size()));
                        moved = true;
                    }
                }

                if (moved) {
                    // Send cursor position to server
                    json cursor_msg = {
                        {"packet_type", "update"},
                        {"data", {
                            {"cursor", { {"x", cursor_x}, {"y", cursor_y} }}
                        }}
                    };
                    send_json(cursor_msg);
                }
            }
        }

        // Update text display
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            std::string full_text;
            for (const auto& line : shared_buffer) {
                full_text += line + "\n";
            }
            text_display.setString(full_text);
        }

        // Update own cursor rectangle
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            std::string current_line = (cursor_y < shared_buffer.size()) ? shared_buffer[cursor_y].substr(0, cursor_x) : "";
            sf::Text temp_text(font);
            temp_text.setFont(font);
            temp_text.setString(current_line);
            temp_text.setCharacterSize(16);

            float x_pos = 10.f + cursor_x * 9.f;
            float y_pos = 10.f + cursor_y * 19.f;
            cursor_rect.setPosition(sf::Vector2f(x_pos, y_pos));
        }

        // Clear window and draw elements
        window.clear(sf::Color::White);
        window.draw(text_display);
        window.draw(cursor_rect);

        // Draw collaborators' cursors
        {
            std::lock_guard<std::mutex> lock(collaborators_mutex);
            for (const auto& [name, collab] : collaborators) {
                // Skip self
                if (name == user_name) continue;

                // Calculate cursor position
                std::string line = (collab.cursor_y < shared_buffer.size()) ? shared_buffer[collab.cursor_y].substr(0, collab.cursor_x) : "";
                sf::Text temp_text(font);
                temp_text.setFont(font);
                temp_text.setString(line);
                temp_text.setCharacterSize(16);
                
                float x_pos = 10.f + collab.cursor_x * 9.f;
                float y_pos = 10.f + collab.cursor_y * 19.f;

                // Draw cursor rectangle
                sf::RectangleShape collab_cursor(sf::Vector2f(2, 20));
                collab_cursor.setFillColor(collab.color);
                collab_cursor.setPosition(sf::Vector2f(x_pos, y_pos));
                window.draw(collab_cursor);

                // Draw collaborator's name
                sf::Text name_text(font);
                name_text.setFont(font);
                name_text.setString(collab.name);
                name_text.setCharacterSize(12);
                name_text.setFillColor(collab.color);
                name_text.setPosition(sf::Vector2f(x_pos + 5.f, y_pos - 15.f));
                window.draw(name_text);
            }
        }

        window.display();
    }

    // Cleanup before exit
    running = false;
    close(sockfd);
    return 0;
}
