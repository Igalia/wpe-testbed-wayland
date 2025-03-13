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

#include "TileRenderer.h"

#include "Application.h"
#include "EGL.h"
#include "GBM.h"
#include "Tile.h"

#include <cassert>
#include <cmath>

TileRenderer::TileRenderer(uint32_t numberOfTiles, uint32_t tileWidth, uint32_t tileHeight, const EGL& egl)
    : m_egl(egl)
    , m_numberOfTiles(numberOfTiles)
    , m_tileWidth(tileWidth)
    , m_tileHeight(tileHeight)
{
    createShaders();
}

TileRenderer::~TileRenderer()
{
    for (auto fence : m_fences)
        m_egl.destroyFence(fence);

    glDeleteProgram(m_program);
    m_tiles.clear();
}

std::unique_ptr<TileRenderer> TileRenderer::create(uint32_t numberOfTiles, uint32_t tileWidth, uint32_t tileHeight, const EGL& egl)
{
    return std::make_unique<TileRenderer>(numberOfTiles, tileWidth, tileHeight, egl);
}

void TileRenderer::initialize(uint32_t screenWidth, uint32_t screenHeight)
{
    m_screenWidth = screenWidth;
    m_screenHeight = screenHeight;

    m_numberOfTileColumns = static_cast<uint32_t>(floor(static_cast<float>(m_screenWidth) / static_cast<float>(m_tileWidth)));
    if (m_numberOfTileColumns > m_numberOfTiles)
        m_numberOfTileColumns = m_numberOfTiles;

    m_numberOfTileRows = static_cast<uint32_t>(ceil(static_cast<float>(m_numberOfTiles) / static_cast<float>(m_numberOfTileColumns)));
}

void TileRenderer::allocateGLTiles()
{
    for (uint32_t i = 0; i < m_numberOfTiles; ++i) {
        m_fences.push_back(nullptr);
        m_tiles.push_back(Tile::createGLTile(m_tileWidth, m_tileHeight));
    }
}

void TileRenderer::allocateDMABufTiles(const DRM& drm, const GBM& gbm)
{
    for (uint32_t i = 0; i < m_numberOfTiles; ++i) {
        m_fences.push_back(nullptr);
        m_tiles.push_back(Tile::createDMABufTile(m_tileWidth, m_tileHeight, drm, gbm, m_egl));
    }
}

static GLuint loadShader(GLenum type, const char* shaderSource)
{
    GLuint shader = glCreateShader(type);
    assert(shader);

    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);

    GLint compiled;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    assert(compiled);

    return shader;
}

void TileRenderer::createShaders()
{
    const char* vertexShaderSource = "uniform mat4 u_mvp;\n"
                                     "attribute vec2 position;\n"
                                     "attribute vec2 texCoord;\n"
                                     "varying vec2 v_texCoord;\n"
                                     "\n"
                                     "void main() {\n"
                                     "    gl_Position = u_mvp * vec4(position, 0.0, 1.0);\n"
                                     "    v_texCoord = texCoord;\n"
                                     "}\n";

    const char* fragmentShaderSource = "precision mediump float;\n"
                                       "varying vec2 v_texCoord;\n"
                                       "uniform sampler2D textureSampler;\n"
                                       "\n"
                                       "void main() {\n"
                                       "    gl_FragColor = texture2D(textureSampler, v_texCoord);\n"
                                       "}\n";

    auto vertexShader = loadShader(GL_VERTEX_SHADER, vertexShaderSource);
    auto fragmentShader = loadShader(GL_FRAGMENT_SHADER, fragmentShaderSource);

    m_program = glCreateProgram();
    assert(m_program);

    glAttachShader(m_program, vertexShader);
    glAttachShader(m_program, fragmentShader);

    glLinkProgram(m_program);

    GLint linked;
    glGetProgramiv(m_program, GL_LINK_STATUS, &linked);
    assert(linked);
}

void TileRenderer::renderTiles()
{
    auto& args = Application::commandLineArguments();

    glViewport(0, 0, m_screenWidth, m_screenHeight);
    if (args.clear) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    for (uint32_t i = 0; i < m_numberOfTiles; ++i) {
        auto& tile = *m_tiles[i].get();
        switch (args.tileUpdateType) {
        case TileUpdateType::ThirdUpdate: {
            auto width = tile.width() / 3;
            auto height = tile.height() / 3;
            auto xOffset = (m_tileWidth - width) / 3;
            auto yOffset = (m_tileHeight - height) / 3;
            auto* rgbaBuffer = tile.createRandomContent(width, height);
            tile.updateContent(xOffset, yOffset, width, height, rgbaBuffer);
            break;
        }
        case TileUpdateType::HalfUpdate: {
            auto width = tile.width() / 2;
            auto height = tile.height() / 2;
            auto xOffset = (m_tileWidth - width) / 2;
            auto yOffset = (m_tileHeight - height) / 2;
            auto* rgbaBuffer = tile.createRandomContent(width, height);
            tile.updateContent(xOffset, yOffset, width, height, rgbaBuffer);
            break;
        }
        case TileUpdateType::FullUpdate:
        default: {
            auto* rgbaBuffer = tile.createRandomContent(tile.width(), tile.height());
            tile.updateContent(0, 0, tile.width(), tile.height(), rgbaBuffer);
            break;
        }
        }

        if (args.fences)
            m_fences[i] = m_egl.createFence();
    }

    int tileIndex = 0;
    for (int row = 0; row < m_numberOfTileRows; row++) {
        for (int column = 0; column < m_numberOfTileColumns; column++) {
            renderTile(m_fences[tileIndex], m_tiles[tileIndex]->id(), column * m_tileWidth, row * m_tileHeight);
            ++tileIndex;

            if (tileIndex == m_numberOfTiles)
                break;
        }
    }
}

static void constructOrthogonalProjectionMatrix(float* m, int mOffset, float left, float right, float bottom, float top, float near, float far)
{
    float r_width = 1.0f / (right - left);
    float r_height = 1.0f / (top - bottom);
    float r_depth = 1.0f / (far - near);
    float x = 2.0f * (r_width);
    float y = -2.0f * (r_height);
    float z = -2.0f * (r_depth);
    float tx = -(right + left) * r_width;
    float ty = (top + bottom) * r_height;
    float tz = -(far + near) * r_depth;

    m[mOffset +  0] = x;
    m[mOffset +  1] = 0.0f;
    m[mOffset +  2] = 0.0f;
    m[mOffset +  3] = 0.0f;
    m[mOffset +  4] = 0.0f;
    m[mOffset +  5] = y;
    m[mOffset +  6] = 0.0f;
    m[mOffset +  7] = 0.0f;
    m[mOffset +  8] = 0.0f;
    m[mOffset +  9] = 0.0f;
    m[mOffset + 10] = z;
    m[mOffset + 11] = 0.0f;
    m[mOffset + 12] = tx;
    m[mOffset + 13] = ty;
    m[mOffset + 14] = tz;
    m[mOffset + 15] = 1.0f;
}

void TileRenderer::renderTile(EGLSyncKHR& fence, GLuint textureID, GLfloat x, GLfloat y)
{
    auto& args = Application::commandLineArguments();
    if (fence) {
        m_egl.clientWaitFence(fence);
        m_egl.destroyFence(fence);
        fence = nullptr;
    }

    float mvp[16];
    constructOrthogonalProjectionMatrix(mvp, 0, 0, m_screenWidth, m_screenHeight, 0, -1000, 1000);

    GLfloat vertices[] = {
        x,
        y,
        x + m_tileWidth,
        y,
        x,
        y + m_tileHeight,
        x + m_tileWidth,
        y + m_tileHeight,
    };

    GLfloat texCoords[] = {
        0.0f,
        0.0f,
        1.0f,
        0.0f,
        0.0f,
        1.0f,
        1.0f,
        1.0f,
    };

    glUseProgram(m_program);

    // Get attribute and uniform locations
    GLint positionLoc = glGetAttribLocation(m_program, "position");
    GLint texCoordLoc = glGetAttribLocation(m_program, "texCoord");
    GLint mvpLoc = glGetUniformLocation(m_program, "u_mvp");
    GLint textureSamplerLoc = glGetUniformLocation(m_program, "textureSampler");

    // Bind the texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glUniform1i(textureSamplerLoc, 0);

    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

    // Set vertex attribute pointers
    glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glEnableVertexAttribArray(positionLoc);

    glVertexAttribPointer(texCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, texCoords);
    glEnableVertexAttribArray(texCoordLoc);

    if (args.blend) {
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
    }

    // Draw the image
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (args.blend)
        glDisable(GL_BLEND);

    // Cleanup
    glDisableVertexAttribArray(positionLoc);
    glDisableVertexAttribArray(texCoordLoc);
    glBindTexture(GL_TEXTURE_2D, 0);
}
