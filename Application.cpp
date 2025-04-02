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

#include "Application.h"

#include "Logger.h"
#include "third_party/argparse.hpp"

#include <cassert>

#include <signal.h>

struct CommandLineArgumentsParser : public argparse::Args {
    int32_t& frameCount    = kwarg("f,frames", "Run for the specified number of frames").set_default(-1);
    uint32_t& tileCount    = kwarg("t,tiles", "Number of tiles to render").set_default(6);
    uint32_t& tileWidth    = kwarg("tile-width", "Tile width").set_default(512);
    uint32_t& tileHeight   = kwarg("tile-height", "Tile height").set_default(512);
    uint32_t& cellSize     = kwarg("cell-size", "Fill pattern cell-size").set_default(32);

    bool& neon             = flag("neon", "Use ARM-NEON instructions when updating texture contents (only valid if --tile-update-method is NOT equal to 'gl')");
    bool& linearFilter     = flag("linear-filter", "Use GL_LINEAR instead of GL_NEAREST for texture min/mag filter");
    bool& depth            = flag("depth", "Enable GL_DEPTH_TEST during tile painting");
    bool& blend            = flag("blend", "Enable GL_BLEND during tile painting");
    bool& explicitSync     = flag("explicit-sync", "Use explicit synchronization protocol");
    bool& noAnimate        = flag("no-animate", "Do not animate color of tile content");
    bool& clear            = flag("clear", "Use glClear() once per frame draw");
    bool& circle           = flag("circle", "Use circle as tile content");
    bool& rbo              = flag("rbo", "Use rbo for painting, as WebKit");
    bool& fences           = flag("fences", "Use fences to synchronize tile rendering");
    bool& opaque           = flag("o,opaque", "Use opaque window surface");
    bool& unbounded        = flag("u,unbounded", "Use unbounded rendering");
    bool& dmabufTiles      = flag("d,dmabuf-tiles", "Use tiles backed up by dmabuf");

    std::string& drmNodeGPU           = kwarg("drm-node-gpu", "DRM node (GPU)").set_default("/dev/dri/card0");
    std::string& drmNodeIPU           = kwarg("drm-node-ipu", "DRM node (IPU)").set_default("/dev/dri/card1");
    std::string& eglPlatform          = kwarg("egl-platform", "EGL platform (gbm|default)").set_default("gbm");
    std::string& tileUpdateType       = kwarg("tile-update-type", "Tile update type (full|half|third)").set_default("full");
    std::string& tileUpdateMethod     = kwarg("tile-update-method", "Tile update method (gl|mmap|gbm)").set_default("gl");
    std::string& tileBufferModifier   = kwarg("tile-buffer-modifier", "Tile buffer DRM modifier, only relevant in --dmabuf-tiles mode (linear|vivante-tiled|vivante-super-tiled)").set_default("linear");
    std::string& windowBufferModifier = kwarg("window-buffer-modifier", "Window buffer DRM modifier (linear|vivante-tiled|vivante-super-tiled)").set_default("linear");

    Application::CommandLineArguments finish() const
    {
        auto parseTileUpdateType = [&]() {
            if (tileUpdateType == "full")
                return TileUpdateType::FullUpdate;

            if (tileUpdateType == "half")
                return TileUpdateType::HalfUpdate;

            if (tileUpdateType == "third")
                return TileUpdateType::ThirdUpdate;

            Logger::error("Invalid --tile-update-type='%s'. Aborting!\n", tileUpdateType.c_str());
            abort();
            return TileUpdateType::FullUpdate;
        };

        auto parseTileUpdateMethod = [&]() {
            if (tileUpdateMethod == "gl")
                return TileUpdateMethod::GLTexSubImage2D;

            if (tileUpdateMethod == "mmap")
                return TileUpdateMethod::MemoryMappingMMAP;

            if (tileUpdateMethod == "gbm")
                return TileUpdateMethod::MemoryMappingGBM;

            Logger::error("Invalid --tile-update-method='%s'. Aborting!\n", tileUpdateMethod.c_str());
            abort();
            return TileUpdateMethod::GLTexSubImage2D;
        };

        auto parseEGLPlatform = [&]() {
            if (eglPlatform == "gbm")
                return EGLPlatform::GBM;

            if (eglPlatform == "default")
                return EGLPlatform::Default;

            Logger::error("Invalid --egl-platform='%s'. Aborting!\n", eglPlatform.c_str());
            abort();
            return EGLPlatform::GBM;
        };

        auto parseTileBufferModifier = [&]() {
            if (tileBufferModifier == "linear")
                return BufferModifier::Linear;

            if (tileBufferModifier == "vivante-tiled")
                return BufferModifier::VivanteTiled;

            if (tileBufferModifier == "vivante-super-tiled")
                return BufferModifier::VivanteSuperTiled;

            Logger::error("Invalid --tile-buffer-modifier='%s'. Aborting!\n", tileBufferModifier.c_str());
            abort();
            return BufferModifier::Linear;
        };

        auto parseWindowBufferModifier = [&]() {
            if (windowBufferModifier == "linear")
                return BufferModifier::Linear;

            if (windowBufferModifier == "vivante-tiled")
                return BufferModifier::VivanteTiled;

            if (windowBufferModifier == "vivante-super-tiled")
                return BufferModifier::VivanteSuperTiled;

            Logger::error("Invalid --window-buffer-modifier='%s'. Aborting!\n", windowBufferModifier.c_str());
            abort();
            return BufferModifier::Linear;
        };

        if (parseTileUpdateMethod() != TileUpdateMethod::GLTexSubImage2D && !dmabufTiles) {
            Logger::error("You cannot use --tile-update-method other than 'gl' without specifying '--dmabuf-tiles'. Aborting!\n");
            abort();
        }

        return { frameCount, tileCount, tileWidth, tileHeight, cellSize, neon, linearFilter, depth, blend, explicitSync, noAnimate, clear, circle, rbo, fences, opaque, unbounded, dmabufTiles, drmNodeGPU, drmNodeIPU, parseEGLPlatform(), parseTileUpdateMethod(), parseTileUpdateType(), parseTileBufferModifier(), parseWindowBufferModifier() };
    }
};

struct Application::Private
{
    Private(int argc, char** argv)
        : args(std::move(argparse::parse<CommandLineArgumentsParser>(argc, argv).finish()))
    {
    }

    bool isRunning { true };
    Application::CommandLineArguments args;
    struct sigaction sigintAction;
};

static Application*& applicationInstance()
{
    static Application* s_application = nullptr;
    return s_application;
}

static void sigintHandler(int)
{
    auto*& application = applicationInstance();
    assert(application);
    application->terminate();
}


Application::Application(int argc, char** argv)
    : d(new Private(argc, argv))
{
    // Parse command line arguments

    // Register SIGINT handler
    d->sigintAction.sa_handler = sigintHandler;
    sigemptyset(&d->sigintAction.sa_mask);
    d->sigintAction.sa_flags = SA_RESETHAND;
    sigaction(SIGINT, &d->sigintAction, nullptr);
}

Application& Application::create(int argc, char** argv)
{
    auto*& application = applicationInstance();
    assert(!application);
    application = new Application(argc, argv);
    return *application;
}

Application::CommandLineArguments& Application::commandLineArguments()
{
    auto*& application = applicationInstance();
    assert(application);
    return application->d->args;
}

void Application::terminate()
{
    d->isRunning = false;
}

bool Application::isRunning() const
{
    return d->isRunning;
}
