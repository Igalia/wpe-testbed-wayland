/* wpe-testbed: WPE/WebKit painting/composition simulation
 *
 * Copyright (C) 2024, 2025 Igalia S.L.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "Tile.h"

#include "Application.h"
#include "DMABuffer.h"
#include "EGL.h"
#include "GBM.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <drm_fourcc.h>
#include <gbm.h>
#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#ifdef __ARM_NEON
#include <arm_neon.h>
#define HAS_NEON 1
#else
#define HAS_NEON 0
#endif

static uint32_t s_tileIndex = 0;

static inline uintptr_t alignUpper(uintptr_t x, uintptr_t alignment)
{
     return (x + alignment - 1) & ~(alignment - 1);
}

Tile::Tile(uint32_t width, uint32_t height)
    : m_width(width)
    , m_height(height)
    , m_tileIndex(++s_tileIndex)
{
    auto& args = Application::commandLineArguments();

    // TODO: Support non-aligned width/height for super-tiled format
    if (args.tileBufferModifier == BufferModifier::VivanteSuperTiled) {
	m_width = alignUpper(m_width, 64 /*superTileSize */);
	m_height = alignUpper(m_height, 64 /* superTileSize */);
    }
}

Tile::~Tile()
{
    glDeleteTextures(1, &m_id);
}

std::unique_ptr<Tile> Tile::createGLTile(uint32_t width, uint32_t height)
{
    auto tile = std::make_unique<Tile>(width, height);
    if (!tile->allocateGLTexture())
        return nullptr;
    return tile;
}

std::unique_ptr<Tile> Tile::createDMABufTile(uint32_t width, uint32_t height, const DRM& drm, const GBM& gbm, const EGL& egl)
{
    auto tile = std::make_unique<Tile>(width, height);
    if (!tile->allocateDMABuf(drm, gbm, egl))
        return nullptr;
    return tile;
}

bool Tile::allocateGLTexture()
{
    auto& args = Application::commandLineArguments();

    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, args.linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, args.linearFilter ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_dmaBufBacked = false;
    return true;
}

bool Tile::allocateDMABuf(const DRM& drm, const GBM& gbm, const EGL& egl)
{
    m_buffer = DMABuffer::create(DMABuffer::Role::TileBuffer, drm, gbm, egl, DRM_FORMAT_ABGR8888, m_width, m_height);
    if (!m_buffer)
        return false;

    m_id = m_buffer->glTexture();
    m_dmaBufBacked = true;
    return true;
}

// Vivante Super Tiled Format

namespace {
    constexpr uint32_t superTileShift = 6;
    constexpr uint32_t superTileSize = 1 << superTileShift;
    constexpr uint32_t superTilePixels = 1 << (2 * superTileShift);
    constexpr uint32_t superTileMask = superTileSize - 1;

    constexpr uint32_t superTile2x2Shift = 5;
    constexpr uint32_t superTile2x2Size = 1 << superTile2x2Shift;
    constexpr uint32_t superTile2x2Pixels = 1 << (2 * superTile2x2Shift);
    constexpr uint32_t superTile2x2Mask = superTile2x2Size - 1;

    constexpr uint32_t superTile4x4Shift = 4;
    constexpr uint32_t superTile4x4Size = 1 << superTile4x4Shift;
    constexpr uint32_t superTile4x4Pixels = 1 << (2 * superTile4x4Shift);
    constexpr uint32_t superTile4x4Mask = superTile4x4Size - 1;

    constexpr uint32_t superTile8x8Shift = 3;
    constexpr uint32_t superTile8x8Size = 1 << superTile8x8Shift;
    constexpr uint32_t superTile8x8Pixels = 1 << (2 * superTile8x8Shift);
    constexpr uint32_t superTile8x8Mask = superTile8x8Size - 1;

    constexpr uint32_t tileShift = 2;
    constexpr uint32_t tileSize = 1 << tileShift;
    constexpr uint32_t tilePixels = 1 << (2 * tileShift);
    constexpr uint32_t tileMask = tileSize - 1;

    constexpr uint32_t stride2x2Pixels = superTileSize * superTile2x2Size;
    constexpr uint32_t stride4x4Pixels = superTile2x2Size * superTile4x4Size;
    constexpr uint32_t stride8x8Pixels = superTile4x4Size * superTile8x8Size;
};

#if HAS_NEON
static void storeLinearBufferInVivanteSuperTiledFormat_NEON(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t,
                                                            const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    for (uint32_t y = 0; y < sh; ++y) {
        const uint32_t yCurrent = dy + y;

        const uint32_t ySuperTile = yCurrent >> superTileShift;
        const uint32_t ySuperTileOffset = yCurrent & superTileMask;

        const uint32_t ySuperTile2x2 = ySuperTileOffset >> superTile2x2Shift;
        const uint32_t ySuperTile2x2Offset = ySuperTileOffset & superTile2x2Mask;

        const uint32_t ySuperTile4x4 = ySuperTile2x2Offset >> superTile4x4Shift;
        const uint32_t ySuperTile4x4Offset = ySuperTile2x2Offset & superTile4x4Mask;

        const uint32_t ySuperTile8x8 = ySuperTile4x4Offset >> superTile8x8Shift;
        const uint32_t ySuperTile8x8Offset = ySuperTile4x4Offset & superTile8x8Mask;

        const uint32_t yTile = ySuperTile8x8Offset >> tileShift;
        const uint32_t yTileOffset = ySuperTile8x8Offset & tileMask;

        const uint32_t tileOffsetRow = ySuperTile * (dw << superTileShift) +
                                       ySuperTile2x2 * stride2x2Pixels +
                                       ySuperTile4x4 * stride4x4Pixels +
                                       ySuperTile8x8 * stride8x8Pixels +
                                       (yTile << superTile2x2Shift) +
                                       yTileOffset * tileSize;

        const uint32_t* srcRow = src + y * spitch;
        for (uint32_t x = 0; x < sw; x += 4) {
            const uint32_t xCurrent = dx + x;
            const uint32_t xSuperTile = xCurrent >> superTileShift;
            const uint32_t xSuperTileOffset = xCurrent & superTileMask;

            const uint32_t xSuperTile2x2 = xSuperTileOffset >> superTile2x2Shift;
            const uint32_t xSuperTile2x2Offset = xSuperTileOffset & superTile2x2Mask;

            const uint32_t xSuperTile4x4 = xSuperTile2x2Offset >> superTile4x4Shift;
            const uint32_t xSuperTile4x4Offset = xSuperTile2x2Offset & superTile4x4Mask;

            const uint32_t xSuperTile8x8 = xSuperTile4x4Offset >> superTile8x8Shift;
            const uint32_t xSuperTile8x8Offset = xSuperTile4x4Offset & superTile8x8Mask;

            const uint32_t xTile = xSuperTile8x8Offset >> tileShift;
            const uint32_t xTileOffset = xSuperTile8x8Offset & tileMask;

            const uint32_t tileIndex = tileOffsetRow +
                                       xTileOffset +
                                       xTile * tilePixels +
                                       xSuperTile * superTilePixels +
                                       xSuperTile2x2 * superTile2x2Pixels +
                                       xSuperTile4x4 * superTile4x4Pixels +
                                       xSuperTile8x8 * superTile8x8Pixels;

            assert(tileIndex + 4 <= dw * dh);

            // Prefetch next source and destination memory
            __builtin_prefetch(srcRow + x + 8, 0, 1);  // Prefetch next cache line (read)
            __builtin_prefetch(dst + tileIndex + 8, 1, 1);  // Prefetch destination (write)

            uint32x4_t vdata = vld1q_u32(srcRow + x);
            vst1q_u32(dst + tileIndex, vdata);
        }
    }
}
#endif

static void storeLinearBufferInVivanteSuperTiledFormat_Generic(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t,
                                                               const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    for (uint32_t y = 0; y < sh; ++y) {
        const uint32_t yCurrent = dy + y;

        const uint32_t ySuperTile = yCurrent >> superTileShift;
        const uint32_t ySuperTileOffset = yCurrent & superTileMask;

        const uint32_t ySuperTile2x2 = ySuperTileOffset >> superTile2x2Shift;
        const uint32_t ySuperTile2x2Offset = ySuperTileOffset & superTile2x2Mask;

        const uint32_t ySuperTile4x4 = ySuperTile2x2Offset >> superTile4x4Shift;
        const uint32_t ySuperTile4x4Offset = ySuperTile2x2Offset & superTile4x4Mask;

        const uint32_t ySuperTile8x8 = ySuperTile4x4Offset >> superTile8x8Shift;
        const uint32_t ySuperTile8x8Offset = ySuperTile4x4Offset & superTile8x8Mask;

        const uint32_t yTile = ySuperTile8x8Offset >> tileShift;
        const uint32_t yTileOffset = ySuperTile8x8Offset & tileMask;

        const uint32_t tileOffsetRow = ySuperTile * (dw << superTileShift) +
                                       ySuperTile2x2 * stride2x2Pixels +
                                       ySuperTile4x4 * stride4x4Pixels +
                                       ySuperTile8x8 * stride8x8Pixels +
                                       (yTile << superTile2x2Shift) +
                                       yTileOffset * tileSize;

        const uint32_t* srcRow = src + y * spitch;
        for (uint32_t x = 0; x < sw; ++x) {
            const uint32_t xCurrent = dx + x;
            const uint32_t xSuperTile = xCurrent >> superTileShift;
            const uint32_t xSuperTileOffset = xCurrent & superTileMask;

            const uint32_t xSuperTile2x2 = xSuperTileOffset >> superTile2x2Shift;
            const uint32_t xSuperTile2x2Offset = xSuperTileOffset & superTile2x2Mask;

            const uint32_t xSuperTile4x4 = xSuperTile2x2Offset >> superTile4x4Shift;
            const uint32_t xSuperTile4x4Offset = xSuperTile2x2Offset & superTile4x4Mask;

            const uint32_t xSuperTile8x8 = xSuperTile4x4Offset >> superTile8x8Shift;
            const uint32_t xSuperTile8x8Offset = xSuperTile4x4Offset & superTile8x8Mask;

            const uint32_t xTile = xSuperTile8x8Offset >> tileShift;
            const uint32_t xTileOffset = xSuperTile8x8Offset & tileMask;

            const uint32_t tileIndex = tileOffsetRow +
                                       xTileOffset +
                                       xTile * tilePixels +
                                       xSuperTile * superTilePixels +
                                       xSuperTile2x2 * superTile2x2Pixels +
                                       xSuperTile4x4 * superTile4x4Pixels +
                                       xSuperTile8x8 * superTile8x8Pixels;

            assert(tileIndex < dw * dh);
            dst[tileIndex] = srcRow[x];
        }
    }
}

inline void storeLinearBufferInVivanteSuperTiledFormat(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                       const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    assert(dw == alignUpper(dw, superTileSize));
    assert(dh == alignUpper(dh, superTileSize));

#if HAS_NEON
    auto& args = Application::commandLineArguments();
    if (args.neon) {
        storeLinearBufferInVivanteSuperTiledFormat_NEON(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
        return;
    }
#endif

    storeLinearBufferInVivanteSuperTiledFormat_Generic(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
}

// Vivante Tiled Format

inline void storeLinearBufferInVivanteTiledFormat_Generic(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                          const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch, uint32_t yStartOffset = 0)
{
    for (uint32_t y = yStartOffset; y < dh; ++y) {
        const uint32_t yCurrent = dy + y;
        const uint32_t yTile = yCurrent >> 2; // := yCurrent / 4
        const uint32_t yLocal = yCurrent & 3; // := yCurrent % 4
        const uint32_t rowRelatedOffset = (yTile * dpitch + yLocal) << 2;

        const uint32_t* srcRow = src + y * spitch;
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t xCurrent = dx + x;
            const uint32_t xTile = xCurrent >> 2; // := xCurrent / 4
            const uint32_t xLocal = xCurrent & 3; // := xCurrent % 4
            dst[rowRelatedOffset + (xTile << 4) + xLocal] = srcRow[x];
        }
    }
}

#if HAS_NEON
inline void storeLinearBufferInVivanteTiledFormat_NEON(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                       const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    const uint32_t dhAligned = alignUpper(dh, tileSize);

    for (uint32_t y = 0; y + 3 < dh; y += 4) { // Process in 4-row blocks
        const uint32_t yCurrent = dy + y;
        const uint32_t yTile = yCurrent >> 2; // := yCurrent / 4
        const uint32_t yLocal = yCurrent & 3; // := yCurrent % 4

        const uint32_t* srcRow0 = src + (y + 0) * spitch;
        const uint32_t* srcRow1 = src + (y + 1) * spitch;
        const uint32_t* srcRow2 = src + (y + 2) * spitch;
        const uint32_t* srcRow3 = src + (y + 3) * spitch;

        const uint32_t rowTileIndex = yTile * dpitch >> 2;
        for (uint32_t x = 0; x + 3 < dw; x += 4) { // Process 4 pixels (1 full tile) at a time
            const uint32_t xCurrent = dx + x;
            const uint32_t xTile = xCurrent >> 2;
            const uint32_t tileBaseOffset = (rowTileIndex + xTile) << 4;

            __builtin_prefetch(srcRow0 + x, 0, 1);
            __builtin_prefetch(srcRow1 + x, 0, 1);
            __builtin_prefetch(srcRow2 + x, 0, 1);
            __builtin_prefetch(srcRow3 + x, 0, 1);
            __builtin_prefetch(dst + tileBaseOffset, 1, 1);

            uint32x4_t row0 = vld1q_u32(srcRow0 + x);
            uint32x4_t row1 = vld1q_u32(srcRow1 + x);
            uint32x4_t row2 = vld1q_u32(srcRow2 + x);
            uint32x4_t row3 = vld1q_u32(srcRow3 + x);

            vst1q_u32(dst + tileBaseOffset, row0);
            vst1q_u32(dst + tileBaseOffset + 1 * sizeof(uint32_t), row1);
            vst1q_u32(dst + tileBaseOffset + 2 * sizeof(uint32_t), row2);
            vst1q_u32(dst + tileBaseOffset + 3 * sizeof(uint32_t), row3);
        }
    }

    // Process remaining rows (if dh is not a multiple of 4)
    storeLinearBufferInVivanteTiledFormat_Generic(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch, dhAligned);
}
#endif

inline void storeLinearBufferInVivanteTiledFormat(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                  const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
#if HAS_NEON
    auto& args = Application::commandLineArguments();
    if (args.neon) {
        storeLinearBufferInVivanteTiledFormat_NEON(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
        return;
    }
#endif

    storeLinearBufferInVivanteTiledFormat_Generic(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
}

// Linear format

#if HAS_NEON
inline void storeLinearBufferInLinearFormat_NEON(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                 const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    constexpr uint32_t numberOfPixelsPerBatch = 16;
    constexpr uint32_t prefetchBytes = numberOfPixelsPerBatch * sizeof(uint32_t);
    const uint32_t dwAligned = alignUpper(dw, numberOfPixelsPerBatch);

    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t* srcRow = src + y * spitch;
        uint32_t* dstRow = dst + (y + dy) * dpitch + dx;

        // Prefetch upcoming rows to reduce memory latency
        __builtin_prefetch(srcRow + prefetchBytes, 0, 1);
        __builtin_prefetch(dstRow + prefetchBytes, 1, 1);

        uint32_t x = 0;
        for (; x < dwAligned; x += numberOfPixelsPerBatch) {
            // Prefetch the next memory block
            __builtin_prefetch(srcRow + x + prefetchBytes, 0, 1);
            __builtin_prefetch(dstRow + x + prefetchBytes, 1, 1);

            uint32x4_t v0 = vld1q_u32(srcRow + x);
            uint32x4_t v1 = vld1q_u32(srcRow + x + 1 * sizeof(uint32_t));
            uint32x4_t v2 = vld1q_u32(srcRow + x + 2 * sizeof(uint32_t));
            uint32x4_t v3 = vld1q_u32(srcRow + x + 3 * sizeof(uint32_t));

            vst1q_u32(dstRow + x, v0);
            vst1q_u32(dstRow + x + 1 * sizeof(uint32_t), v1);
            vst1q_u32(dstRow + x + 2 * sizeof(uint32_t), v2);
            vst1q_u32(dstRow + x + 3 * sizeof(uint32_t), v3);
        }

        // Handle remaining pixels
        for (; x < dw; ++x)
            dstRow[x] = srcRow[x];
    }
}
#endif

inline void storeLinearBufferInLinearFormat_Generic(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                                    const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
    constexpr uint32_t numberOfPixelsPerBatch = 16;
    constexpr uint32_t prefetchBytes = numberOfPixelsPerBatch * sizeof(uint32_t);
    const uint32_t dwAligned = alignUpper(dw, numberOfPixelsPerBatch);

    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t* srcRow = src + y * spitch;
        uint32_t* dstRow = dst + (y + dy) * dpitch + dx;

        // Prefetch upcoming rows to reduce memory latency
        __builtin_prefetch(srcRow + prefetchBytes, 0, 1);
        __builtin_prefetch(dstRow + prefetchBytes, 1, 1);

        uint32_t x = 0;
        for (; x < dwAligned; x += numberOfPixelsPerBatch) {
            // Prefetch the next memory block
            __builtin_prefetch(srcRow + x + prefetchBytes, 0, 1);
            __builtin_prefetch(dstRow + x + prefetchBytes, 1, 1);

            for (uint32_t i = 0; i < numberOfPixelsPerBatch; ++i)
                dstRow[x + i] = srcRow[x + i];
        }

        // Handle remaining pixels
        for (; x < dw; ++x)
            dstRow[x] = srcRow[x];
    }
}

inline void storeLinearBufferInLinearFormat(uint32_t* dst, uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh, uint32_t dpitch,
                                            const uint32_t* src, uint32_t sw, uint32_t sh, uint32_t spitch)
{
#if HAS_NEON
    auto& args = Application::commandLineArguments();
    if (args.neon) {
        storeLinearBufferInLinearFormat_NEON(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
        return;
    }
#endif

    storeLinearBufferInLinearFormat_Generic(dst, dx, dy, dw, dh, dpitch, src, sw, sh, spitch);
}

void Tile::updateContentGL(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data)
{
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexSubImage2D(GL_TEXTURE_2D, 0, xOffset, yOffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, data);
}

void Tile::updateContentGBM(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data)
{
    uint32_t dstStride = 0;
    void* mapData = nullptr;
    void* destAddress = gbm_bo_map(m_buffer->gbmBufferObject(), 0, 0, m_width, m_height, GBM_BO_TRANSFER_WRITE, &dstStride, &mapData);

    const uint32_t srcPitch = width;
    const uint32_t dstPitch = dstStride / sizeof(uint32_t);
    storeLinearBufferInLinearFormat(reinterpret_cast<uint32_t*>(destAddress), xOffset, yOffset, m_width, m_height, dstPitch, reinterpret_cast<uint32_t*>(data), width, height, srcPitch);

    gbm_bo_unmap(m_buffer->gbmBufferObject(), mapData);
}

void Tile::updateContentMMAP(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data)
{
    const uint32_t srcPitch = width;
    const uint32_t dstStride = gbm_bo_get_stride(m_buffer->gbmBufferObject());
    const uint32_t dstPitch = dstStride / sizeof(uint32_t);
    assert(dstStride >= m_width);

    int dmaBufFD = m_buffer->dmabufFDForPlane(0);

    static void* mmapAddress = nullptr;
    if (!mmapAddress) {
        assert(m_height == gbm_bo_get_height(m_buffer->gbmBufferObject()));
        mmapAddress = mmap(nullptr, dstStride * m_height, PROT_WRITE, MAP_SHARED, dmaBufFD, 0);
        assert(mmapAddress != MAP_FAILED);
    }
    void* destAddress = mmapAddress;

    const struct dma_buf_sync syncStart = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
    ioctl(dmaBufFD, DMA_BUF_IOCTL_SYNC, &syncStart);

    auto& args = Application::commandLineArguments();
    switch (args.tileBufferModifier) {
    case BufferModifier::VivanteTiled:
        storeLinearBufferInVivanteTiledFormat(reinterpret_cast<uint32_t*>(destAddress), xOffset, yOffset, m_width, m_height, dstPitch, reinterpret_cast<uint32_t*>(data), width, height, srcPitch);
        break;
    case BufferModifier::VivanteSuperTiled:
        storeLinearBufferInVivanteSuperTiledFormat(reinterpret_cast<uint32_t*>(destAddress), xOffset, yOffset, m_width, m_height, dstPitch, reinterpret_cast<uint32_t*>(data), width, height, srcPitch);
        break;
    case BufferModifier::Linear:
        storeLinearBufferInLinearFormat(reinterpret_cast<uint32_t*>(destAddress), xOffset, yOffset, m_width, m_height, dstPitch, reinterpret_cast<uint32_t*>(data), width, height, srcPitch);
        break;
    }

    const struct dma_buf_sync syncEnd = { DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE };
    ioctl(dmaBufFD, DMA_BUF_IOCTL_SYNC, &syncEnd);
}

void Tile::updateContent(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data)
{
    auto& args = Application::commandLineArguments();

    if (args.tileUpdateMethod == TileUpdateMethod::GLTexSubImage2D) {
        updateContentGL(xOffset, yOffset, width, height, data);
        return;
    }

    assert(m_dmaBufBacked);
    if (args.tileUpdateMethod == TileUpdateMethod::MemoryMappingMMAP) {
        updateContentMMAP(xOffset, yOffset, width, height, data);
        return;
    }

    assert(args.tileUpdateMethod == TileUpdateMethod::MemoryMappingGBM);
    updateContentGBM(xOffset, yOffset, width, height, data);
}

uint8_t* Tile::createRandomContent(uint32_t width, uint32_t height) const
{
    auto& args = Application::commandLineArguments();

    using RGBAColor = std::tuple<uint8_t, uint8_t, uint8_t, uint8_t>;
    static std::vector<RGBAColor> colors = {
        {255, 0, 0, 255},      // Red
        {0, 255, 0, 255},      // Green
        {0, 0, 255, 255},      // Blue
        {255, 255, 0, 255},    // Yellow
        {255, 165, 0, 255},    // Orange
        {0, 255, 255, 255},    // Cyan
        {255, 0, 255, 255},    // Magenta
        {128, 0, 128, 255}     // Purple
    };

    static uint8_t* rgbaBuffer = nullptr;
    if (!rgbaBuffer)
        rgbaBuffer = static_cast<uint8_t*>(std::aligned_alloc(64, width * height * 4));
    else if (args.noAnimate)
        return rgbaBuffer;

    static uint32_t s_animationIndex = 0;
    auto cellSize = args.cellSize * m_tileIndex;

    auto fillPixelWithColor = [&](int x, int y, const RGBAColor& color) {
        int offset = (y * width + x) * 4;
        rgbaBuffer[offset] = std::get<0>(color);
        rgbaBuffer[offset + 1] = std::get<1>(color);
        rgbaBuffer[offset + 2] = std::get<2>(color);
        rgbaBuffer[offset + 3] = std::get<3>(color);
    };

    auto fillPixelAutoColor = [&](int x, int y) {
        // Assuming cellSize is a power of 2
        int shift = __builtin_ctz(cellSize);  // Count trailing zeros = log2(cellSize)

        // If colors.size() is a power of 2, replace % with bitwise AND
        int colorMask = colors.size() - 1;  // Only if colors.size() is power of 2

        // Optimized computation
        int colorIndex = ((x >> shift) + (y >> shift) + s_animationIndex) & colorMask;

        fillPixelWithColor(x, y, colors[colorIndex]);
    };

    if (args.circle) {
        auto cx = width / 2;
        auto cy = height / 2;
        auto radius = std::min(width, height) / 2;
        for (int y = cy - radius; y <= cy + radius; ++y) {
            for (int x = cx - radius; x <= cx + radius; ++x) {
                int dx = x - cx;
                int dy = y - cy;
                if (dx * dx + dy * dy <= radius * radius) {
                    if (x >= 0 && x < width && y >= 0 && y < height)
                        fillPixelAutoColor(x, y);
                }
            }
        }
    } else {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x)
                fillPixelAutoColor(x, y);
        }
    }

    if (!args.noAnimate)
        ++s_animationIndex;

    return rgbaBuffer;
}
