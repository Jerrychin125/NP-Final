# server/Makefile

CXX = g++
CXXFLAGS = -std=c++17 -Wall -I./include
LDFLAGS = -pthread

TARGET = build/server
SRC = src/main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -rf build/

.PHONY: all clean
