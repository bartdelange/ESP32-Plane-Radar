#pragma once

/**
 * Streaming PNG decoder for the terrain layer. Production decoding allocates
 * temporary scratch only after the tile's TLS connection has closed and never
 * borrows display memory.
 *
 * This exists instead of LovyanGFX's bundled pngle so the working set stays
 * explicit, bounded and testable. It lives in platform/ because core/ remains
 * independent of graphics libraries.
 */

#include <cstddef>
#include <cstdint>

#include "core/platform.h"
#include "core/terrain.h"

namespace platform_png {

/**
 * Scratch the decoder requires, in one contiguous piece:
 *
 *   32768  the DEFLATE sliding window, whose size RFC 1951 fixes
 *    2048  two unfiltered scanlines (a 256 px RGB row is 768 bytes)
 *    1024  Huffman decode tables for the literal, distance and length trees
 *
 * This 35.8 KiB allocation is the decoder's complete temporary working set.
 */
constexpr size_t kScratchBytes = 32768 + 2048 + 1024;

/**
 * Optional scratch injection for deterministic tests. Production leaves this
 * unset and the decoder owns a temporary allocation.
 */
using ScratchFn = uint8_t* (*)(size_t need_bytes);
using ScratchReleaseFn = void (*)();
void setScratch(ScratchFn fn, ScratchReleaseFn release = nullptr);

/**
 * Decode a PNG straight off an HTTP body, invoking `on_pixel` once per pixel in
 * raster order. Only the window and two scanlines are held, so the raster never
 * exists in memory and tile size costs nothing.
 *
 * Accepts what the terrain tiles are — 8-bit truecolour RGB, non-interlaced, up
 * to 256 px wide — and rejects anything else rather than guessing. Returns false
 * on a malformed or truncated image, or when temporary scratch is unavailable.
 */
bool decode(core::platform::BodyReader& body, core::terrain::PixelFn on_pixel,
            void* ctx);

}  // namespace platform_png

