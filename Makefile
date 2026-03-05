# SPACE GL - 3D LOGIC ENGINE
# Authors: Nicola Taibi, Supported by Google Gemini
# Copyright (C) 2026 Nicola Taibi
# Licensed under GPL-3.0-or-later

# --- Configurazione Compilatore ---
CC       := gcc
GLSLC    := glslc
BIN_DIR  := .
SRC_DIR  := src
INC_DIR  := include
OBJ_DIR  := build/obj
SHD_DIR  := build/shaders

# --- Flag di Compilazione ---
# -MMD -MP generano automaticamente le dipendenze degli header
CFLAGS   := -std=c2x -Wall -Wextra -Wpedantic -I$(INC_DIR) -D_XOPEN_SOURCE=700 -g -O3 -fopenmp
CFLAGS   += -fPIE -fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat -Wformat-security
DEPFLAGS := -MMD -MP

# --- Librerie ---
GL_LIBS  := -lglut -lGLU -lGL -lGLEW
VK_LIBS  := -lglfw -lvulkan
SHM_LIBS := -lrt -lpthread -lcrypto -lm -lgomp
HUD_LIBS := -lncurses
LDFLAGS  := -pie -Wl,-z,relro,-z,now

# --- Target e Sorgenti ---
TARGETS := spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_hud spacegl_vulkan

# Server: include i moduli della sottodirectory src/server/
SERVER_SRCS := $(SRC_DIR)/spacegl_server.c \
               $(wildcard $(SRC_DIR)/server/*.c)

# Shaders Vulkan
SHADERS_SRC := $(wildcard assets/shaders/*.vert assets/shaders/*.frag)
SHADERS_SPV := $(patsubst assets/shaders/%, $(SHD_DIR)/%.spv, $(SHADERS_SRC))

# Mappatura oggetti
SERVER_OBJS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SERVER_SRCS))

# --- Regole ---
.PHONY: all clean help shaders check

all: $(TARGETS) shaders

# Binari Principali
spacegl_server: $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS) $(SHM_LIBS)

spacegl_client: $(OBJ_DIR)/spacegl_client.o
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS) $(SHM_LIBS)

spacegl_viewer: $(OBJ_DIR)/spacegl_viewer.o
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS) $(SHM_LIBS)

spacegl_3dview: $(OBJ_DIR)/spacegl_3dview.o
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS) $(GL_LIBS) $(SHM_LIBS)

spacegl_hud: $(OBJ_DIR)/spacegl_hud.o
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $^ -o $(BIN_DIR)/$@ $(LDFLAGS) $(HUD_LIBS) $(SHM_LIBS)

spacegl_vulkan: $(OBJ_DIR)/spacegl_vulkan.o $(SHADERS_SPV)
	@echo "Linking $@"
	@$(CC) $(CFLAGS) $< -o $(BIN_DIR)/$@ $(LDFLAGS) $(VK_LIBS) $(SHM_LIBS)

# Regola generica per i file oggetto
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

# Compilazione Shader
shaders: $(SHADERS_SPV)

$(SHD_DIR)/%.spv: assets/shaders/%
	@mkdir -p $(dir $@)
	@echo "Compiling shader $<"
	@$(GLSLC) $< -o $@

# Inclusione automatica delle dipendenze (.d)
-include $(shell find $(OBJ_DIR) -name "*.d" 2>/dev/null)

check: all
	@echo "Running verification..."
	@for t in $(TARGETS); do test -x $$t || (echo "Error: $$t missing"; exit 1); done
	@echo "All components ready."

clean:
	@echo "Cleaning artifacts..."
	@rm -rf build/
	@rm -f $(TARGETS)

help:
	@echo "SpaceGL Professional Makefile"
	@echo "Usage:"
	@echo "  make            Build everything"
	@echo "  make -j\$(nproc)  Build faster (parallel)"
	@echo "  make clean      Remove all build files"
	@echo "  make shaders    Recompile only Vulkan shaders"
	@echo "  make check      Verify binaries"
