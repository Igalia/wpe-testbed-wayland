/* wpe-testbed: WPE/WebKit painting/composition simulation
 *
 * Copyright (C) 2024 Igalia S.L.
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

#pragma once

#include <cstdint>
#include <memory>

#include <GLES2/gl2.h>

class DMABuffer;
class DRM;
class EGL;
class GBM;

class Tile {
public:
    Tile(uint32_t width, uint32_t height);
    ~Tile();

    static std::unique_ptr<Tile> createGLTile(uint32_t width, uint32_t height);
    static std::unique_ptr<Tile> createDMABufTile(uint32_t width, uint32_t height, const DRM&, const GBM&, const EGL&);

    GLuint id() const { return m_id; }
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

    uint8_t* createRandomContent(uint32_t width, uint32_t height) const;
    void updateContent(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data);

private:
    bool allocateGLTexture();
    bool allocateDMABuf(const DRM&, const GBM&, const EGL&);

    void updateContentGL(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data);
    void updateContentGBM(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data);
    void updateContentMMAP(uint32_t xOffset, uint32_t yOffset, uint32_t width, uint32_t height, uint8_t* data);

    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    uint32_t m_tileIndex { 0 };

    GLuint m_id { 0 };

    bool m_dmaBufBacked { false };
    std::unique_ptr<DMABuffer> m_buffer;
};
