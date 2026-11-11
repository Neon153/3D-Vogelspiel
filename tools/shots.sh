#!/bin/bash
# Testbilder mit dem Software-Rasterizer lavapipe (nur fuer die Entwicklung unter Linux)
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json
cd "$(dirname "$0")/.."
name=$1; shift
timeout 1500 xvfb-run -a -s "-screen 0 1920x1080x24" ./build/vogelflug --size ${W:-960} ${H:-540} --density ${DENS:-0.3} --shot out/$name.ppm ${FRAMES:-14} "$@" > out/$name.log 2>&1
python3 -c "
from PIL import Image
Image.open('out/$name.ppm').save('out/$name.png')"
