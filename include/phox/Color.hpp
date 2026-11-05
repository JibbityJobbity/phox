#ifndef PHOX_COLOR_HPP
#define PHOX_COLOR_HPP

#include "phox/Math.hpp"

#include <cmath>

/// Phox renders in linear light: material colours, light colours and the clear colour are linear, and
/// the tonemap pass encodes the result for the display with the sRGB transfer function. Colours picked
/// on screen or in an image editor are sRGB-encoded; convert them with srgb() first.
namespace phox {

/// The sRGB electro-optical transfer function: an encoded value in [0, 1] to linear light.
inline float srgbToLinear(float encoded) {
    return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

/// The inverse of srgbToLinear for values in [0, 1].
inline float linearToSrgb(float linear) {
    return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

inline vec3 srgbToLinear(const vec3 &encoded) {
    return {srgbToLinear(encoded.r), srgbToLinear(encoded.g), srgbToLinear(encoded.b)};
}

inline vec3 linearToSrgb(const vec3 &linear) {
    return {linearToSrgb(linear.r), linearToSrgb(linear.g), linearToSrgb(linear.b)};
}

/// A linear RGBA colour from sRGB-encoded components (alpha is linear already).
inline vec4 srgb(float r, float g, float b, float a = 1.0f) { return {srgbToLinear(vec3(r, g, b)), a}; }

} // namespace phox

#endif // PHOX_COLOR_HPP
