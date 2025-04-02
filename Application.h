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
#include <string>

enum class TileUpdateMethod {
    GLTexSubImage2D,
    MemoryMappingMMAP,
    MemoryMappingGBM
};

enum class TileUpdateType {
    FullUpdate,
    HalfUpdate,
    ThirdUpdate
};

enum class BufferModifier {
    Linear,
    VivanteTiled,
    VivanteSuperTiled
};

enum class EGLPlatform {
    GBM,
    Wayland
};

class Application {
public:
    static Application& create(int argc, char** argv);

    struct CommandLineArguments {
        int32_t frameCount { 0 };
        uint32_t tileCount { 0 };
        uint32_t tileWidth { 0 };
        uint32_t tileHeight { 0 };
        uint32_t cellSize { 0 };

        bool neon { false };
        bool linearFilter { false };
        bool depth { false };
        bool blend { false };
        bool explicitSync { false };
        bool noAnimate { false };
        bool clear { false };
        bool circle { false };
        bool rbo { false };
        bool fences { false };
        bool opaque { false };
        bool unbounded { false };
        bool dmabufTiles { false };

        std::string drmNodeGPU;
        std::string drmNodeIPU;

        EGLPlatform eglPlatform { EGLPlatform::GBM };
        TileUpdateMethod tileUpdateMethod { TileUpdateMethod::GLTexSubImage2D };
        TileUpdateType tileUpdateType { TileUpdateType::FullUpdate };
        BufferModifier tileBufferModifier { BufferModifier::Linear };
        BufferModifier windowBufferModifier { BufferModifier::Linear };
    };

    static CommandLineArguments& commandLineArguments();

    void terminate();
    bool isRunning() const;

private:
    Application(int argc, char** argv);

    struct Private;
    Private* d { nullptr };
};
