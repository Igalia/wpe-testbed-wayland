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

#include <memory>
#include <vector>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

class DRM;
class EGL;
class GBM;
class Tile;

class TileRenderer {
public:
    TileRenderer(uint32_t numberOfTiles, uint32_t tileWidth, uint32_t tileHeight, const EGL&);
    ~TileRenderer();

    static std::unique_ptr<TileRenderer> create(uint32_t numberOfTiles, uint32_t tileWidth, uint32_t tileHeight, const EGL&);

    void initialize(uint32_t screenWidth, uint32_t screenHeight);

    void allocateGLTiles();
    void allocateDMABufTiles(const DRM&, const GBM&);

    void renderTiles();

private:
    void createShaders();

    void renderTile(EGLSyncKHR&, GLuint textureID, GLfloat x, GLfloat y);

    const EGL& m_egl;
    GLuint m_program { 0 };

    uint32_t m_screenWidth { 0 };
    uint32_t m_screenHeight { 0 };

    uint32_t m_tileWidth { 0 };
    uint32_t m_tileHeight { 0 };

    uint32_t m_numberOfTiles { 0 };
    uint32_t m_numberOfTileColumns { 0 };
    uint32_t m_numberOfTileRows { 0 };

    std::vector<EGLSyncKHR> m_fences;
    std::vector<std::unique_ptr<Tile>> m_tiles;
};
