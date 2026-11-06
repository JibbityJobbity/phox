// Shared by the fluid sample's compute shaders. Every pass runs one invocation per cell of an n x n grid
// (cells indexed row by row, y up), in 8 x 8 groups.
layout(local_size_x = 8, local_size_y = 8) in;

const uint kMaxSplats = 3; // FluidInputs::splats beyond this are ignored (push constants are 128 bytes)

// A splat (fluid::Splat): xy position in [0, 1]^2, zw force; then radius, amount (dye), unused, unused.
struct Splat {
    vec4 positionForce;
    vec4 radiusAmount;
};

bool outside(ivec2 c, uint n) { return any(lessThan(c, ivec2(0))) || any(greaterThanEqual(c, ivec2(n))); }
uint cellIndex(ivec2 c, uint n) { return uint(c.y) * n + uint(c.x); }
// The cell's centre in [0, 1]^2.
vec2 cellCentre(ivec2 c, uint n) { return (vec2(c) + 0.5) / float(n); }

// The splats' summed weight at p: a Gaussian of the splat's radius.
float splatWeight(Splat s, vec2 p) {
    const vec2 d = p - s.positionForce.xy;
    return exp(-dot(d, d) / (s.radiusAmount.x * s.radiusAmount.x));
}
