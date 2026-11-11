// world.h - prozedurale Welt: verlassene Stadt, Wald, Fluss, Vogel-Modell, Kollision
#pragma once
#include "mathx.h"
#include <vector>
#include <cstdint>

struct Vtx {           // 32 Byte, entspricht struct Vtx in rt.comp (std430)
    float px, py, pz;
    uint32_t n;        // Normale, oktaedrisch 2x16 Bit
    float u, v;
    uint32_t col;      // sqrt(Albedo) rgb + a
    uint32_t mat;      // Material-ID | A<<8 | B<<16 | C<<24
};

enum Mat : uint32_t {
    M_TERRAIN = 0, M_ROAD, M_SIDEWALK, M_FAC_CONCRETE, M_FAC_BRICK, M_FAC_GLASS, M_ROOF, M_CONCRETE,
    M_BARK, M_LEAF, M_GRASS, M_CARPAINT, M_METAL, M_WATER, M_FEATHER, M_CARGLASS, M_BEAK
};

struct MeshB {
    std::vector<Vtx> v;
    std::vector<uint32_t> io;   // opake Dreiecke
    std::vector<uint32_t> il;   // Blattkarten (immer Quad-Paare, Alpha prozedural im Shader)
    uint32_t leafType = 1;      // 1 Laub, 2 Nadeln, 3 Efeu
    bool minor = false;         // Kleinkram (Gras): wird von Nebel-/GI-Strahlen uebersprungen
};

struct Inst { float m[12]; uint32_t mesh; };
struct Box { vec3 mn, mx; };

struct World {
    std::vector<MeshB> chunks;  // statische Welt, je Chunk ein BLAS
    std::vector<MeshB> meshes;  // instanzierte Meshes (Baeume, Gras, Autos, Vogelteile ...)
    std::vector<Inst> insts;
    std::vector<Box> boxes;     // Kollisions-Quader (Gebaeude, Bruecke)
    // Vogel: ein zusammenhaengendes Netz in Ruhepose + Biege-Gewichte (3 je Vertex), wird pro Frame verformt
    std::vector<Vtx> birdRest; std::vector<uint32_t> birdIdx; std::vector<float> birdW;
    void poseBird(Vtx* out, float shoulder, float wrist, float tip, float sweep) const;
    float waterLevel = -1.0f;
    float bridgeX = 0;
    float cityX0 = -540, cityX1 = 540, cityZ0 = -500, cityZ1 = 500; // von Strassen/Bloecken bedecktes Rechteck
    vec3 spawn{0, 60, 0};

    void generate(float density, uint32_t seed);
    float height(float x, float z) const;
    float riverZ(float x) const { return 700.f + 55.f * std::sin(x / 270.f); }
    // Kugel gegen Welt; schiebt p heraus, liefert Kontaktnormale
    bool collide(vec3& p, float r, vec3& n) const;
    bool insideBox(const vec3& p, float margin) const;

private:
    // Kollisionsgitter
    float gx0 = 0, gz0 = 0, gcell = 32; int gnx = 0, gnz = 0;
    std::vector<std::vector<uint32_t>> grid;
    void buildGrid();
};
