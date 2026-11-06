// Smoothed particle hydrodynamics in the unit square (fluid::SmoothedParticles). Particles are processed
// over a side x side grid of invocations (particle = y * side + x); the neighbour search uses bins x bins
// square bins at least h wide, so a particle's neighbours are in the 3 x 3 bins around its own.
// Keep the kernels in step with SmoothedParticles.cpp and the fluid tests.

struct SphCommon {
    uint side;          // particles = side * side
    uint bins;          // bins a side
    float h;            // smoothing radius
    float mass;         // of each particle
    float restDensity;  // in the middle of the tank (unused by the shaders; it sets the stiffness)
    float stiffness;    // pressure = stiffness * (density / latticeDensity(position) - 1)
    float viscosity;
    float dt;
};

const float kPi = 3.14159265358979;

// 2D kernels after Mueller et al. 2003: poly6 for density, the spiky gradient for pressure, and the
// viscosity kernel's Laplacian.
float poly6(float r2, float h) {
    const float d = h * h - r2;
    return d > 0.0 ? 4.0 / (kPi * pow(h, 8.0)) * d * d * d : 0.0;
}
vec2 spikyGradient(vec2 r, float h) {
    const float len = length(r);
    if (len <= 0.0 || len >= h) return vec2(0.0);
    return -30.0 / (kPi * pow(h, 5.0)) * (h - len) * (h - len) * (r / len);
}
float viscosityLaplacian(float len, float h) { return len < h ? 40.0 / (kPi * pow(h, 5.0)) * (h - len) : 0.0; }

// The density the initial lattice of particles (one at each cell centre of the side x side grid) has at x
// with kernel radius h. In the middle of the tank it is the rest density; near a wall it is less, by
// exactly what the wall's missing neighbours take away.
float latticeDensity(vec2 x, float h, uint side, float mass) {
    float density = 0.0;
    const int reach = int(ceil(h * float(side)));
    const ivec2 nearest = ivec2(floor(x * float(side)));
    for (int dy = -reach; dy <= reach; ++dy) {
        for (int dx = -reach; dx <= reach; ++dx) {
            const ivec2 q = nearest + ivec2(dx, dy);
            if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, ivec2(side)))) continue;
            const vec2 r = x - (vec2(q) + 0.5) / float(side);
            density += mass * poly6(dot(r, r), h);
        }
    }
    return density;
}

ivec2 binOf(vec2 p, uint bins) { return clamp(ivec2(floor(p * float(bins))), ivec2(0), ivec2(bins - 1)); }
uint binIndex(ivec2 b, uint bins) { return uint(b.y) * bins + uint(b.x); }
