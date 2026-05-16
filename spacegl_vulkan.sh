#!/bin/bash
# Script di avvio per il visore Vulkan
# Si assicura che il working directory sia la cartella build per trovare gli shader
cd build && ./spacegl_vulkan "$@"
