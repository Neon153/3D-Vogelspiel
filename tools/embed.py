#!/usr/bin/env python3
# Bettet die kompilierten SPIR-V-Shader als C-Arrays in src/shaders.h ein (die .exe braucht keine Zusatzdateien).
import sys, struct, os
out = ["// automatisch erzeugt von tools/embed.py", "#pragma once", "#include <cstdint>", "#include <cstddef>"]
for path in sys.argv[2:]:
    name = os.path.basename(path).replace(".spv", "").replace(".", "_")
    data = open(path, "rb").read()
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    out.append(f"static const uint32_t SPV_{name}[] = {{" + ",".join("0x%x" % w for w in words) + "};")
    out.append(f"static const size_t SPV_{name}_size = sizeof(SPV_{name});")
open(sys.argv[1], "w").write("\n".join(out) + "\n")
