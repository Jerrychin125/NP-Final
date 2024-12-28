# # client/Makefile

# CXX = g++
# CXXFLAGS = -std=c++17 -Wall -I./include -I/usr/include
# LDFLAGS = -pthread -lsfml-graphics -lsfml-window -lsfml-system

# TARGET = build/client
# SRC = src/main.cpp

# all: $(TARGET)

# $(TARGET): $(SRC)
# 	@mkdir -p build
# 	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

# clean:
# 	rm -rf build/

# .PHONY: all clean

# Compiler
CXX = clang++

# Compiler flags
CXXFLAGS = -std=c++17 -Wall -I./include -I/usr/local/include

# Libraries and frameworks
LIBS = -L/usr/local/lib \
       -lsfml-graphics -lsfml-window -lsfml-system \
       -framework Cocoa -framework OpenGL -framework IOKit -framework CoreVideo -pthread \
       -Wl,-rpath,/usr/local/lib

# Target and source files
TARGET = build/client
SRC = src/main.cpp

# Default target
all: $(TARGET)

# Build the target
$(TARGET): $(SRC)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LIBS)

# Clean target
clean:
	rm -rf build/

.PHONY: all clean