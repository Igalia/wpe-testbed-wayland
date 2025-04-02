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

#include "WaylandWindow.h"

#include "Application.h"
#include "DMABuffer.h"
#include "EGL.h"
#include "GBM.h"
#include "Logger.h"
#include "TileRenderer.h"
#include "Wayland.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "linux-explicit-synchronization-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <cassert>

#include <signal.h>
#include <unistd.h>

/* XDG surface */
static void xdg_surface_configure(void*, struct xdg_surface* xdg_surface, uint32_t serial)
{
    // confirm that you exist to the compositor
    xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};
/* (end) XDG surface */

/* XDG toplevel */
static void xdg_toplevel_on_configure(void* data, struct xdg_toplevel* toplevel, int32_t width, int32_t height, struct wl_array* states)
{
    assert(width > 0);
    assert(height > 0);

    auto& waylandWindow = *reinterpret_cast<WaylandWindow*>(data);
    waylandWindow.setSize(width, height);

    auto& args = Application::commandLineArguments();
    if (args.opaque) {
        struct wl_region* region = wl_compositor_create_region(waylandWindow.wayland().compositor());
        wl_region_add(region, 0, 0, width, height);
        wl_surface_set_opaque_region(waylandWindow.surface(), region);
        wl_region_destroy(region);
    }

    wl_surface_commit(waylandWindow.surface());
}

static void xdg_toplevel_on_close(void*, struct xdg_toplevel*)
{
    // The main loop handles SIGINT and exits gracefully.
    raise(SIGINT);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_on_configure,
    .close = xdg_toplevel_on_close,
};
/* (end) XDG toplevel */

static void buffer_release(void* data, struct wl_buffer* buffer)
{
    auto& dmaBuffer = *static_cast<DMABuffer*>(data);
    dmaBuffer.setIsInUse(false);
}

static const struct wl_buffer_listener buffer_listener = {
    buffer_release
};

static void create_succeeded(void* data, struct zwp_linux_buffer_params_v1* params, struct wl_buffer* new_buffer)
{
    auto& dmaBuffer = *static_cast<DMABuffer*>(data);
    dmaBuffer.setWaylandBuffer(new_buffer);

    auto& args = Application::commandLineArguments();
    if (!args.explicitSync)
        wl_buffer_add_listener(new_buffer, &buffer_listener, data);

    zwp_linux_buffer_params_v1_destroy(params);
}

static void create_failed(void* data, struct zwp_linux_buffer_params_v1* params)
{
    auto& dmaBuffer = *static_cast<DMABuffer*>(data);
    zwp_linux_buffer_params_v1_destroy(params);
    Logger::error("zwp_linux_buffer_params_v1.create failed.\n");
}

static const struct zwp_linux_buffer_params_v1_listener params_listener = {
    .created = create_succeeded,
    .failed = create_failed
};

static void render_frame(void* data, struct wl_callback* callback, uint32_t)
{
    auto& waylandWindow = *reinterpret_cast<WaylandWindow*>(data);
    waylandWindow.renderFrame(callback);
}

static const struct wl_callback_listener frame_listener = {
    render_frame
};

static void buffer_fenced_release(void* data, struct zwp_linux_buffer_release_v1* release, int32_t fence)
{
    auto& dmaBuffer = *static_cast<DMABuffer*>(data);

    assert(release == dmaBuffer.zwpLinuxBufferReleaseV1());
    assert(dmaBuffer.releaseFenceFD() == -1);

    dmaBuffer.setIsInUse(false);
    dmaBuffer.setReleaseFenceFD(fence);

    zwp_linux_buffer_release_v1_destroy(dmaBuffer.zwpLinuxBufferReleaseV1());
    dmaBuffer.setBufferRelease(nullptr);
}

static void buffer_immediate_release(void* data, struct zwp_linux_buffer_release_v1* release)
{
    auto& dmaBuffer = *static_cast<DMABuffer*>(data);

    assert(release == dmaBuffer.zwpLinuxBufferReleaseV1());
    assert(dmaBuffer.releaseFenceFD() == -1);

    dmaBuffer.setIsInUse(false);
    zwp_linux_buffer_release_v1_destroy(dmaBuffer.zwpLinuxBufferReleaseV1());
    dmaBuffer.setBufferRelease(nullptr);
}

static const struct zwp_linux_buffer_release_v1_listener buffer_release_listener = {
    buffer_fenced_release,
    buffer_immediate_release,
};

WaylandWindow::WaylandWindow(const Wayland& wayland)
    : m_wayland(wayland)
{
    m_statistics.initialize();
}

WaylandWindow::~WaylandWindow()
{
}

void WaylandWindow::setTileRenderer(std::unique_ptr<TileRenderer>&& tileRenderer)
{
    m_tileRenderer = std::move(tileRenderer);
}

std::unique_ptr<WaylandWindow> WaylandWindow::create(const Wayland& wayland)
{
    auto waylandWindow = std::make_unique<WaylandWindow>(wayland);
    assert(waylandWindow->m_waitForConfigure);
    waylandWindow->createSurface();
    assert(!waylandWindow->m_waitForConfigure);
    return waylandWindow;
}

bool WaylandWindow::dmaBufferAssignmentFinished() const
{
    for (uint32_t i = 0; i < numBuffers; ++i) {
        if (!m_buffers[i]->wlBuffer())
            return false;
    }

    return true;
}

bool WaylandWindow::createBuffers()
{
    auto& args = Application::commandLineArguments();
    for (uint32_t i = 0; i < numBuffers; ++i) {
        auto dmaBuffer = DMABuffer::create(DMABuffer::Role::WindowBuffer, m_wayland.drm(), m_wayland.gbm(), m_wayland.egl(), m_wayland.format(), m_width, m_height);
        if (!dmaBuffer)
            return false;

        struct zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params(m_wayland.zwpLinuxDmabufV1());
        for (uint32_t i = 0; i < dmaBuffer->planeCount(); ++i) {
            auto modifier = dmaBuffer->modifier();
            zwp_linux_buffer_params_v1_add(params, dmaBuffer->dmabufFDForPlane(i), i, dmaBuffer->offsetForPlane(i), dmaBuffer->strideForPlane(i), modifier >> 32, modifier & 0xffffffff);
        }

        zwp_linux_buffer_params_v1_add_listener(params, &params_listener, dmaBuffer.get());
        zwp_linux_buffer_params_v1_create(params, dmaBuffer->width(), dmaBuffer->height(), dmaBuffer->format(), 0);
        m_buffers[i] = std::move(dmaBuffer);
    }

    while (!dmaBufferAssignmentFinished())
        wl_display_roundtrip(m_wayland.display());

    return true;
}

void WaylandWindow::createSurface()
{
    Logger::info("Creating Wayland surface...\n");

    m_wlSurface = wl_compositor_create_surface(m_wayland.compositor());
    assert(m_wlSurface);

    if (auto* xdgWmBase = m_wayland.xdgWmBase()) {
        m_xdgSurface = xdg_wm_base_get_xdg_surface(xdgWmBase, m_wlSurface);
        xdg_surface_add_listener(m_xdgSurface, &xdg_surface_listener, nullptr);

        m_xdgToplevel = xdg_surface_get_toplevel(m_xdgSurface);
        xdg_toplevel_add_listener(m_xdgToplevel, &xdg_toplevel_listener, this);
        xdg_toplevel_set_title(m_xdgToplevel, "wpe-testbed-wayland");
        xdg_toplevel_set_fullscreen(m_xdgToplevel, nullptr);
    }

    if (auto* zwpLinuxExplicitSynchronizationV1 = m_wayland.zwpLinuxExplicitSynchronizationV1()) {
        m_zwpLinuxSurfaceSynchronizationV1 = zwp_linux_explicit_synchronization_v1_get_synchronization(zwpLinuxExplicitSynchronizationV1, m_wlSurface);
        assert(m_zwpLinuxSurfaceSynchronizationV1);
    }

    wl_surface_commit(m_wlSurface);

    while (m_waitForConfigure)
        wl_display_roundtrip(m_wayland.display());
}

void WaylandWindow::setSize(uint32_t width, uint32_t height)
{
    m_width = width;
    m_height = height;
    m_waitForConfigure = false;
}

DMABuffer* WaylandWindow::obtainBuffer()
{
    auto& args = Application::commandLineArguments();
    if (args.unbounded)
        return m_buffers[0].get();

    for (uint32_t i = 0; i < numBuffers; ++i) {
        if (!m_buffers[i]->isInUse())
            return m_buffers[i].get();
    }

    return nullptr;
}

void WaylandWindow::renderFrame(struct wl_callback* callback)
{
    auto& args = Application::commandLineArguments();
    auto* dmaBuffer = obtainBuffer();
    if (!dmaBuffer) {
        Logger::error(callback ? "All buffers busy at redraw(). Server bug?\n" : "Failed to create the first buffer.\n");
        abort();
    }

    /* Start fps measuring on second frame, to remove the time spent
     * compiling shader, etc, from the fps:
     */
    if (m_statistics.currentFrame() == 1)
        m_statistics.initialize();

    glBindFramebuffer(GL_FRAMEBUFFER, dmaBuffer->glFrameBuffer());

    if (args.depth) {
        glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_DEPTH_TEST);
    }

    static bool s_initialized = false;
    if (!s_initialized) {
        s_initialized = true;
        m_tileRenderer->initialize(m_width, m_height);
    }
    m_tileRenderer->renderTiles();

    if (args.depth)
        glDisable(GL_DEPTH_TEST);

    if (m_wayland.useExplicitSync()) {
        auto fenceFD = m_wayland.egl().createFenceFD();
        zwp_linux_surface_synchronization_v1_set_acquire_fence(m_zwpLinuxSurfaceSynchronizationV1, fenceFD);
        close(fenceFD);

        dmaBuffer->setBufferRelease(zwp_linux_surface_synchronization_v1_get_release(m_zwpLinuxSurfaceSynchronizationV1));
        zwp_linux_buffer_release_v1_add_listener(dmaBuffer->zwpLinuxBufferReleaseV1(), &buffer_release_listener, dmaBuffer);
    } else
        glFlush();

    m_statistics.advanceFrame();

    wl_surface_attach(m_wlSurface, dmaBuffer->wlBuffer(), 0, 0);
    wl_surface_damage(m_wlSurface, 0, 0, width(), height());

    if (!args.unbounded) {
        if (callback)
            wl_callback_destroy(callback);

        m_wlCallback = wl_surface_frame(m_wlSurface);
        wl_callback_add_listener(m_wlCallback, &frame_listener, this);
    }

    wl_surface_commit(m_wlSurface);

    dmaBuffer->setIsInUse(true);
    m_statistics.reportFrameRate();
}

void WaylandWindow::executeRenderLoop(Application& app)
{
    // Issue first frame
    assert(!m_waitForConfigure);
    renderFrame(nullptr);

    auto& args = app.commandLineArguments();

    int32_t ret = 0;
    if (args.unbounded)
        ret = wl_display_flush(m_wayland.display());

    while (app.isRunning() && m_statistics.currentFrame() <= args.frameCount && ret != -1) {
        if (!args.unbounded)
            ret = wl_display_dispatch(m_wayland.display());
        else
            renderFrame(nullptr);
    }

    m_statistics.reportFrameRate(true);
}
