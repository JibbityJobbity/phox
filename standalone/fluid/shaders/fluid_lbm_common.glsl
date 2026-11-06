// D2Q9 lattice: rest, the four axes, then the four diagonals.
const ivec2 kE[9] = ivec2[9](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(-1, 0), ivec2(0, -1), ivec2(1, 1),
                             ivec2(-1, 1), ivec2(-1, -1), ivec2(1, -1));
const float kW[9] = float[9](4.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0, 1.0 / 9.0, 1.0 / 36.0, 1.0 / 36.0,
                             1.0 / 36.0, 1.0 / 36.0);
const int kOpposite[9] = int[9](0, 3, 4, 1, 2, 7, 8, 5, 6);

float equilibrium(int i, float rho, vec2 u) {
    const float eu = dot(vec2(kE[i]), u);
    return kW[i] * rho * (1.0 + 3.0 * eu + 4.5 * eu * eu - 1.5 * dot(u, u));
}
