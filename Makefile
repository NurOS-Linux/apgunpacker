CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iexternal

SRC = src/main.cpp
OBJ = build/main.o
BIN = build/apgunpacker

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(OBJ) -o $(BIN) -larchive

build/%.o: src/%.cpp
	mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build
