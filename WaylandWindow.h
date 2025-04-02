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

#include <array>
#include <cstdint>
#include <memory>

#include <wayland-client.h>
#include <wayland-egl.h>

#include "Statistics.h"

class Application;
class DMABuffer;
class TileRenderer;
class Wayland;

class WaylandWindow {
public:
    WaylandWindow(const Wayland&);
    ~WaylandWindow();

    static constexpr uint32_t numBuffers = 4;
    static std::unique_ptr<WaylandWindow> create(const Wayland&);

    bool createBuffers();
    void setTileRenderer(std::unique_ptr<TileRenderer>&&);

    void executeRenderLoop(Application&);
    void renderFrame(struct wl_callback*);

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    void setSize(uint32_t width, uint32_t height);

    const Wayland& wayland() const { return m_wayland; }
    struct wl_surface* surface() const { return m_wlSurface; }

private:
    void createSurface();
    bool dmaBufferAssignmentFinished() const;
    DMABuffer* obtainBuffer();

    const Wayland& m_wayland;
    struct wl_surface* m_wlSurface { nullptr };
    struct xdg_surface* m_xdgSurface { nullptr };
    struct xdg_toplevel* m_xdgToplevel { nullptr };
    struct wl_callback* m_wlCallback { nullptr };
    struct zwp_linux_surface_synchronization_v1* m_zwpLinuxSurfaceSynchronizationV1 { nullptr };

    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    bool m_waitForConfigure { true };

    alignas(8) Statistics m_statistics;

    std::unique_ptr<TileRenderer> m_tileRenderer;
    std::array<std::unique_ptr<DMABuffer>, numBuffers> m_buffers;
};
