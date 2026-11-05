#ifndef PHOX_METAL_COMMON_HPP
#define PHOX_METAL_COMMON_HPP

// The Metal backend's headers expose Metal objects (id<MTL...>), so they need Objective-C++ with ARC.
// Only phox/metal/Renderer.hpp and phox/metal/ShaderInterface.hpp can be included from plain C++.
#if !defined(__OBJC__) || !__has_feature(objc_arc)
#error "phox/metal headers need Objective-C++ with ARC (compile as .mm with -fobjc-arc)"
#endif

#import <Metal/Metal.h>

#endif // PHOX_METAL_COMMON_HPP
