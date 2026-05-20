CXX ?= g++
TARGET := bin/os_process_monitor
SRC := src/main.cpp

CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic -O2
PKG_CONFIG ?= pkg-config

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf 2>/dev/null)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf 2>/dev/null)

ifeq ($(strip $(SDL_LIBS)),)
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null) -lSDL2_image -lSDL2_ttf
endif

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) $< -o $@ $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf bin
