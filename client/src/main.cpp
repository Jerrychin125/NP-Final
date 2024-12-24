// client/src/main.cpp

#include <SFML/Graphics.hpp>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <libgen.h> // For dirname

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

// Function to send JSON messages over the socket
void send_json(const json& message) {
    std::string msg_str = message.dump() + "\n";
    ssize_t n = send(sockfd, msg_str.c_str(), msg_str.length(), 0);
    if (n < 0) {
        perror("Send error");
    }
}

// Function to handle incoming messages from the server
void receive_messages() {
    char buffer[4096];
    while (running) {
        ssize_t n = recv(sockfd, buffer, sizeof(buffer)-1, 0);
        if (n > 0) {
            buffer[n] = '\0';
            std::string msg_str(buffer, n);
            size_t pos = 0;
            while ((pos = msg_str.find('\n')) != std::string::npos) {
                std::string line = msg_str.substr(0, pos);
                msg_str.erase(0, pos + 1);
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
                            std::cout << "Connected to server successfully.\n";
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
                            std::cout << "User '" << c.name << "' connected.\n";
                        }
                        else if (event == "user_disconnected") {
                            std::string name = message["data"]["user"]["name"];
                            std::lock_guard<std::mutex> lock(collaborators_mutex);
                            collaborators.erase(name);
                            std::cout << "User '" << name << "' disconnected.\n";
                        }
                    }
                    else if (message["packet_type"] == "buffer_update") {
                        std::lock_guard<std::mutex> lock(buffer_mutex);
                        shared_buffer = message["data"]["buffer"].get<std::vector<std::string>>();
                    }
                    else if (message["packet_type"] == "cursor_update") {
                        std::string name = message["data"]["user"];
                        int x = message["data"]["cursor"]["x"];
                        int y = message["data"]["cursor"]["y"];
                        std::lock_guard<std::mutex> lock(collaborators_mutex);
                        if (collaborators.find(name) != collaborators.end()) {
                            collaborators[name].cursor_x = x;
                            collaborators[name].cursor_y = y;
                        }
                        else {
                            // New collaborator (if not already in the list)
                            Collaborator c;
                            c.name = name;
                            c.color = sf::Color::Red; // Default color, could assign dynamically
                            c.cursor_x = x;
                            c.cursor_y = y;
                            collaborators[name] = c;
                        }
                    }
                }
                catch (json::parse_error& e) {
                    std::cerr << "JSON parse error: " << e.what() << std::endl;
                }
            }
        }
        else if (n == 0) {
            std::cout << "Server closed the connection.\n";
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

int main() {
    // Initialize SFML window
    sf::RenderWindow window(sf::VideoMode(800, 600), "CoVim Client");
    window.setVerticalSyncEnabled(false);
    window.setFramerateLimit(60);

    // Load font
    sf::Font font;
    if (!font.loadFromFile("Arial.ttf")) {
        std::cerr << "Failed to load Arial.ttf. Attempting to load OpenSans.ttf." << std::endl;
        if (!font.loadFromFile("client/resources/fonts/OpenSans.ttf")) {
            std::cerr << "Failed to load OpenSans.ttf." << std::endl;
            // Handle the error appropriately, e.g., exit or use a default font.
        }
    }

    // Setup text display
    sf::Text text_display;
    text_display.setFont(font);
    text_display.setCharacterSize(16);
    text_display.setFillColor(sf::Color::Black);
    text_display.setPosition(10, 10);

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
    if ( (sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0 ) {
        perror("Socket creation failed");
        return -1;
    }

    // Server address
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(server_port);

    if (inet_pton(AF_INET, server_ip.c_str(), &servaddr.sin_addr) <= 0 ) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        return -1;
    }

    // Connect to server
    if ( connect(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0 ) {
        perror("Connection Failed");
        close(sockfd);
        return -1;
    }

    // Start a thread to receive messages
    std::thread recv_thread(receive_messages);

    // Prompt for username
    std::cout << "Enter your username: ";
    std::cin >> user_name;
    std::cin.ignore(); // Ignore remaining newline

    // Send username as JSON
    json username_msg = { {"name", user_name} };
    send_json(username_msg);

    // Main loop variables
    int cursor_x = 0;
    int cursor_y = 0;

    while (window.isOpen() && running) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                window.close();
                running = false;
                break;
            }

            // Handle text input
            if (event.type == sf::Event::TextEntered) {
                if (event.text.unicode == '\b') { // Backspace
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    if (!shared_buffer.empty()) {
                        if (cursor_x > 0) {
                            shared_buffer[cursor_y].erase(shared_buffer[cursor_y].begin() + cursor_x - 1);
                            cursor_x--;
                        }
                        else if (cursor_y > 0) {
                            cursor_x = shared_buffer[cursor_y - 1].size();
                            shared_buffer[cursor_y - 1] += shared_buffer[cursor_y];
                            shared_buffer.erase(shared_buffer.begin() + cursor_y);
                            cursor_y--;
                        }
                    }

                    // Send update to server
                    json update_msg = {
                        {"packet_type", "update"},
                        {"data", {
                            {"buffer", shared_buffer},
                            {"cursor", { {"x", cursor_x}, {"y", cursor_y} }}
                        }}
                    };
                    send_json(update_msg);
                }
                else if (event.text.unicode == '\r') { // Enter key
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    std::string new_line = shared_buffer[cursor_y].substr(cursor_x);
                    shared_buffer[cursor_y] = shared_buffer[cursor_y].substr(0, cursor_x);
                    shared_buffer.insert(shared_buffer.begin() + cursor_y + 1, new_line);
                    cursor_y++;
                    cursor_x = 0;

                    // Send update to server
                    json update_msg = {
                        {"packet_type", "update"},
                        {"data", {
                            {"buffer", shared_buffer},
                            {"cursor", { {"x", cursor_x}, {"y", cursor_y} }}
                        }}
                    };
                    send_json(update_msg);
                }
                else if (event.text.unicode >= 32 && event.text.unicode <= 126) { // Printable characters
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    if (cursor_y < shared_buffer.size()) {
                        shared_buffer[cursor_y].insert(shared_buffer[cursor_y].begin() + cursor_x, static_cast<char>(event.text.unicode));
                        cursor_x++;
                    }

                    // Send update to server
                    json update_msg = {
                        {"packet_type", "update"},
                        {"data", {
                            {"buffer", shared_buffer},
                            {"cursor", { {"x", cursor_x}, {"y", cursor_y} }}
                        }}
                    };
                    send_json(update_msg);
                }
            }

            // Handle key presses for navigation
            if (event.type == sf::Event::KeyPressed) {
                bool moved = false;
                std::lock_guard<std::mutex> lock(buffer_mutex);
                if (event.key.code == sf::Keyboard::Left) {
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
                if (event.key.code == sf::Keyboard::Right) {
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
                if (event.key.code == sf::Keyboard::Up) {
                    if (cursor_y > 0) {
                        cursor_y--;
                        cursor_x = std::min(cursor_x, static_cast<int>(shared_buffer[cursor_y].size()));
                        moved = true;
                    }
                }
                if (event.key.code == sf::Keyboard::Down) {
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
            sf::Text temp_text;
            temp_text.setFont(font);
            temp_text.setString(current_line);
            temp_text.setCharacterSize(16);
            float text_width = temp_text.getLocalBounds().width;
            float x_pos = 10 + text_width;
            float y_pos = 10 + cursor_y * 20;
            cursor_rect.setPosition(x_pos, y_pos);
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
                sf::Text temp_text;
                temp_text.setFont(font);
                temp_text.setString(line);
                temp_text.setCharacterSize(16);
                float text_width = temp_text.getLocalBounds().width;
                float x_pos = 10 + text_width;
                float y_pos = 10 + collab.cursor_y * 20;

                // Draw cursor rectangle
                sf::RectangleShape collab_cursor(sf::Vector2f(2, 20));
                collab_cursor.setFillColor(collab.color);
                collab_cursor.setPosition(x_pos, y_pos);
                window.draw(collab_cursor);

                // Draw collaborator's name
                sf::Text name_text;
                name_text.setFont(font);
                name_text.setString(collab.name);
                name_text.setCharacterSize(12);
                name_text.setFillColor(collab.color);
                name_text.setPosition(x_pos + 5, y_pos - 15);
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
