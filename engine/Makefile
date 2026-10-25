# Build with:  mingw32-make        (Windows / MSYS2)
#              make                (Linux / macOS)
CXX      ?= g++
EXE      ?= chessai
# ARCH=native is fastest locally; release builds use a portable target (e.g. ARCH=x86-64-v2)
ARCH     ?= native
CXXFLAGS ?= -std=c++20 -O3 -march=$(ARCH) -flto -DNDEBUG -Wall -Wextra
LDFLAGS  ?= -flto -pthread

ifeq ($(OS),Windows_NT)
    EXE := $(EXE).exe
    LDFLAGS += -static -Wl,--stack,16777216
endif

SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:src/%.cpp=build/%.o)

$(EXE): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.cpp $(wildcard src/*.h) | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -rf build $(EXE)

.PHONY: clean
