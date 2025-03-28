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
#include "DRM.h"
#include "EGL.h"
#include "GBM.h"
#include "Logger.h"
#include "TileRenderer.h"
#include "Wayland.h"
#include "WaylandWindow.h"

int main(int argc, char** argv)
{
    auto& app = Application::create(argc, argv);
    auto& args = app.commandLineArguments();

    auto drmIPU = DRM::createForNode(args.drmNodeIPU);
    if (!drmIPU) {
        Logger::error("Failed to initialize DRM (IPU)\n");
        return -1;
    }

    auto gbmIPU = GBM::create(drmIPU->fd());
    if (!gbmIPU) {
        Logger::error("Failed to initialize GBM (IPU)\n");
        return -1;
    }

    std::unique_ptr<DRM> drmGPU;
    std::unique_ptr<GBM> gbmGPU;
    if (args.drmNodeIPU != args.drmNodeGPU) {
        drmGPU = DRM::createForNode(args.drmNodeGPU);

        if (!drmGPU) {
            Logger::error("Failed to initialize DRM (GPU)\n");
            return -1;
        }

        gbmGPU = GBM::create(drmGPU->fd());
        if (!gbmGPU) {
            Logger::error("Failed to initialize GBM (GPU)\n");
            return -1;
        }
    }

    auto egl = EGL::create(*gbmIPU);
    if (!egl) {
        Logger::error("Failed to initialize EGL\n");
        return -1;
    }

    auto wayland = Wayland::create(*drmIPU, *gbmIPU, *egl);
    if (!wayland) {
        Logger::error("Failed to initialize Wayland\n");
        return -1;
    }

    auto tileRenderer = TileRenderer::create(args.tileCount, args.tileWidth, args.tileHeight, *egl);
    if (!tileRenderer) {
        Logger::error("Failed to initialize tile rendering\n");
        return -1;
    }

    if (args.dmabufTiles)
        tileRenderer->allocateDMABufTiles(drmGPU ? *drmGPU : *drmIPU, gbmGPU ? *gbmGPU : *gbmIPU);
    else
        tileRenderer->allocateGLTiles();

    auto waylandWindow = WaylandWindow::create(*wayland.get(), std::move(tileRenderer));
    if (!waylandWindow) {
        Logger::error("Failed to initialize Wayland window\n");
        return -1;
    }

    Logger::info("Starting. Executing render loop...\n");
    waylandWindow->executeRenderLoop(app);

    Logger::info("Exiting. Cleaning up resources...\n");
    waylandWindow.reset();
    egl.reset();
    gbmGPU.reset();
    gbmIPU.reset();
    drmGPU.reset();
    drmIPU.reset();

    return 0;
}
