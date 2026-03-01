# SPACE GL - 3D LOGIC ENGINE 
# Authors: Nicola Taibi, Supported by Google Gemini
# Copyright (C) 2026 Nicola Taibi
# Licensed under GPL-3.0-or-later
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

CC = gcc
OPT_CFLAGS := -g -O2 -fPIE -fopenmp
CFLAGS += -Wall -Iinclude -std=c2x -D_XOPEN_SOURCE=700 $(OPT_CFLAGS)
GL_LIBS = -lglut -lGLU -lGL -lGLEW
VK_LIBS = -lglfw -lvulkan
SHM_LIBS = -lrt -lpthread -lcrypto -lm -pie -lgomp
LDFLAGS += 

.PHONY: all check clean shaders

all: spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_hud spacegl_vulkan shaders

SERVER_SRCS = src/spacegl_server.c src/server/galaxy.c src/server/net.c src/server/commands.c src/server/logic.c src/server/threadpool.c

spacegl_server: $(SERVER_SRCS)
	$(CC) $(SERVER_SRCS) -o spacegl_server $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_viewer: src/spacegl_viewer.c
	$(CC) src/spacegl_viewer.c -o spacegl_viewer $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_client: src/spacegl_client.c
	$(CC) src/spacegl_client.c -o spacegl_client $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_3dview: src/spacegl_3dview.c
	$(CC) src/spacegl_3dview.c -o spacegl_3dview $(CFLAGS) $(LDFLAGS) $(GL_LIBS) $(SHM_LIBS)

spacegl_hud: src/spacegl_hud.c
	$(CC) src/spacegl_hud.c -o spacegl_hud $(CFLAGS) -lncurses $(SHM_LIBS)

spacegl_vulkan: src/spacegl_vulkan.c shaders
	$(CC) src/spacegl_vulkan.c -o spacegl_vulkan $(CFLAGS) $(VK_LIBS) $(SHM_LIBS)

shaders: build/shaders/shader.vert.spv build/shaders/shader.frag.spv

build/shaders/shader.vert.spv: assets/shaders/shader.vert
	@mkdir -p build/shaders
	glslc assets/shaders/shader.vert -o build/shaders/shader.vert.spv

build/shaders/shader.frag.spv: assets/shaders/shader.frag
	@mkdir -p build/shaders
	glslc assets/shaders/shader.frag -o build/shaders/shader.frag.spv

check: all
	test -x spacegl_server
	test -x spacegl_client
	test -x spacegl_3dview
	test -x spacegl_viewer
	test -x spacegl_hud
	test -x spacegl_vulkan

clean:
	rm -f spacegl_server spacegl_client spacegl_3dview spacegl_viewer spacegl_hud spacegl_vulkan
	rm -rf build/shaders
