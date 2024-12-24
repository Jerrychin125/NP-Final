# client/Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -I./include -I/usr/include
LDFLAGS = -pthread -lsfml-graphics -lsfml-window -lsfml-system

TARGET = build/client
SRC = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf build/

.PHONY: all clean
