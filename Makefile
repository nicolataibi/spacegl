# SPACE GL - 3D LOGIC ENGINE 
# Authors: Nicola Taibi, Supported by Google Gemini
# Copyright (C) 2026 Nicola Taibi
# Licensed under GPL-3.0-or-later
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

CC = gcc
OPT_CFLAGS := -g -O2 -fPIE
CFLAGS += -Wall -Iinclude -std=c2x -D_XOPEN_SOURCE=700 $(OPT_CFLAGS)
GL_LIBS = -lglut -lGLU -lGL -lGLEW
SHM_LIBS = -lrt -lpthread -lcrypto -lm -pie
LDFLAGS += 

.PHONY: all check clean

all: spacegl_server spacegl_client spacegl_3dview spacegl_viewer

SERVER_SRCS = src/spacegl_server.c src/server/galaxy.c src/server/net.c src/server/commands.c src/server/logic.c

spacegl_server: $(SERVER_SRCS)
	$(CC) $(SERVER_SRCS) -o spacegl_server $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_viewer: src/spacegl_viewer.c
	$(CC) src/spacegl_viewer.c -o spacegl_viewer $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_client: src/spacegl_client.c
	$(CC) src/spacegl_client.c -o spacegl_client $(CFLAGS) $(LDFLAGS) $(SHM_LIBS)

spacegl_3dview: src/spacegl_3dview.c
	$(CC) src/spacegl_3dview.c -o spacegl_3dview $(CFLAGS) $(LDFLAGS) $(GL_LIBS) $(SHM_LIBS)

check: all
	test -x spacegl_server
	test -x spacegl_client
	test -x spacegl_3dview
	test -x spacegl_viewer

clean:
	rm -f spacegl_server spacegl_client spacegl_3dview spacegl_viewer
