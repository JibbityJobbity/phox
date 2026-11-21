// The task and mesh shaders of a generated heightfield (GeneratedDraw::heightfield): a square grid of
// heights drawn straight from the buffer the generator wrote, with no vertex buffer in between. Mirrors
// include/phox/vulkan/ShaderInterface.hpp (GridDrawPushConstants) and, for the surface it makes, what
// the fluid sample's vertex path writes (fluid_surface.comp): the same points, the same normals.
//
// The grid is cut into tiles of PHOX_GRID_TILE cells a side (fewer at the far edges). One task shader
// workgroup takes one tile: it finds the tile's lowest and highest points, drops the tile if it lies
// outside the view, picks how coarsely the tile is drawn, and launches one mesh shader workgroup for it,
// which emits the tile's vertices and triangles.
//
// Level of detail: a tile uses every 1st, 2nd or 4th grid point, the coarsest whose merged cells are at
// most draw.lodThreshold pixels wide. Tiles of different detail meet without cracks because a tile's edge
// along a coarser neighbour is the straight line between the neighbour's points: the vertices of the finer
// tile in between are moved onto it (snappedHeight).

#extension GL_EXT_mesh_shader : require
#extension GL_EXT_buffer_reference : require
#include "common.glsl"

#define PHOX_GRID_TILE 8

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer HeightBuffer { float values[]; };

const uint kGridFrustumCulling = 1u;

layout(push_constant) uniform GridDrawPushConstants {
    mat4 model;
    HeightBuffer heights; // resolution * resolution floats, row by row
    uint resolution;      // grid points a side
    float size;           // world units a side, before model
    uint flags;           // kGridFrustumCulling
    uint material;        // index into `materials`
    float lodThreshold;   // the widest, in pixels, a merged cell may be
    uint padding;
} draw;

// From the task shader to the mesh shader.
struct GridTask {
    uvec2 tile;
    // Detail as a shift (1 << shift grid points a step): this tile's, then its neighbours' to the left
    // (-x), right (+x), below (-z) and above (+z), each in 2 bits. A neighbour that does not exist counts
    // as the tile itself.
    uint shifts;
};

uint tileCount() { return (draw.resolution - 1u + PHOX_GRID_TILE - 1u) / PHOX_GRID_TILE; }

// The tile's size in cells: PHOX_GRID_TILE, less at the far edges.
uvec2 tileCells(uvec2 tile) {
    return min(uvec2(PHOX_GRID_TILE), uvec2(draw.resolution - 1u) - tile * PHOX_GRID_TILE);
}

float heightAt(ivec2 c) {
    c = clamp(c, ivec2(0), ivec2(draw.resolution - 1u));
    return draw.heights.values[uint(c.y) * draw.resolution + uint(c.x)];
}

// The grid point's position in the surface's plane (before model); +z is the grid's +y.
vec2 gridXZ(vec2 c) { return (c / float(draw.resolution - 1u) - 0.5) * draw.size; }

// The largest scale of the transform.
float modelScale() {
    return sqrt(max(max(dot(draw.model[0].xyz, draw.model[0].xyz), dot(draw.model[1].xyz, draw.model[1].xyz)),
                    dot(draw.model[2].xyz, draw.model[2].xyz)));
}

// How coarsely @p tile is drawn, from its distance alone (not its heights), so every tile that asks about
// it gets one answer. `precise`: the tile and its neighbours each evaluate this, and must agree bit for
// bit however the compiler fuses the arithmetic.
uint tileShift(uvec2 tile) {
    uvec2 cells = tileCells(tile);
    precise vec2 middle = gridXZ(vec2(tile * PHOX_GRID_TILE) + vec2(cells) * 0.5);
    precise vec3 centre = (draw.model * vec4(middle.x, 0.0, middle.y, 1.0)).xyz;
    precise float cell = draw.size / float(draw.resolution - 1u) * modelScale();
    precise float radius = 0.5 * length(vec2(cells)) * cell;
    precise float distance = max(length(centre - frame.cameraPosition.xyz) - radius, 1e-6);
    precise float pixelsPerUnit = abs(frame.proj[1][1]) * 0.5 * frame.resolution.y;
    precise float pixelsPerCell = cell * pixelsPerUnit / distance;
    for (uint shift = 2u; shift > 0u; --shift) {
        uint step = 1u << shift;
        if (cells.x % step == 0u && cells.y % step == 0u && pixelsPerCell * float(step) <= draw.lodThreshold) {
            return shift;
        }
    }
    return 0u;
}
