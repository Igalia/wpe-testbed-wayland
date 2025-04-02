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

#include "Wayland.h"

#include "Application.h"
#include "EGL.h"
#include "Logger.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "linux-explicit-synchronization-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include <drm_fourcc.h>

/* Registry Listener */
static void xdg_wm_base_ping(void*, struct xdg_wm_base* xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void dmabuf_modifiers(void* data, struct zwp_linux_dmabuf_v1*, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo)
{
    auto& wayland = *reinterpret_cast<Wayland*>(data);
    wayland.setDMABufModifiers(format, (static_cast<uint64_t>(modifier_hi) << 32) + static_cast<uint64_t>(modifier_lo));
}

static void dmabuf_format(void*, struct zwp_linux_dmabuf_v1*, uint32_t)
{
    // deprecated
}

static const struct zwp_linux_dmabuf_v1_listener dmabuf_listener = {
    .format = dmabuf_format,
    .modifier = dmabuf_modifiers
};

static void registry_global(void* data, struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version)
{
    auto& wayland = *static_cast<Wayland*>(data);
    wayland.registerInterface(registry, id, interface, version);
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = nullptr
};
/* (end) Registry Listener */

/* XDG surface */
static void xdg_surface_configure(void* data, struct xdg_surface* xdg_surface,
    uint32_t serial)
{
    // confirm that you exist to the compositor
    xdg_surface_ack_configure(xdg_surface, serial);
}

const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};
/* (end) XDG surface */

Wayland::Wayland(struct wl_display* display, const DRM& drm, const GBM& gbm)
    : m_wlDisplay(display)
    , m_drm(drm)
    , m_gbm(gbm)
{
    auto& args = Application::commandLineArguments();
    m_format = args.opaque ? DRM_FORMAT_XRGB8888 : DRM_FORMAT_ARGB8888;

    m_wlRegistry = wl_display_get_registry(m_wlDisplay);
    assert(m_wlRegistry);

    wl_registry_add_listener(m_wlRegistry, &registry_listener, this);
    wl_display_dispatch(m_wlDisplay);
    wl_display_roundtrip(m_wlDisplay);

    assert(m_wlCompositor);
    assert(m_xdgWmBase);
}

const EGL& Wayland::egl() const
{
    assert(m_initialized);
    assert(m_egl);
    return *m_egl;
}

void Wayland::initializeWithEGL(const EGL& egl)
{
    assert(!m_initialized);
    m_egl = &egl;

    auto& args = Application::commandLineArguments();
    if (args.explicitSync) {
        m_useExplicitSync = egl.supportsExplicitSync() && m_zwpLinuxExplicitSynchronizationV1;

        if (!egl.supportsExplicitSync()) {
            Logger::error("EGL does not support the required extension for explicit sync. Aborting!\n");
            abort();
        }

        if (!m_zwpLinuxExplicitSynchronizationV1) {
            Logger::error("Wayland zwp_linux_explicit_synchronization_v1 protocol not supported, cannot use explicit sync. Aborting!\n");
            abort();
        }
    } else
        m_useExplicitSync = false;

    m_initialized = true;
}

Wayland::~Wayland()
{
    if (m_modifiers)
        free(m_modifiers);
}

void Wayland::setDMABufModifiers(uint32_t format, uint64_t modifier)
{
    if (format != m_format)
        return;

    m_formatSupported = true;

    if (modifier != DRM_FORMAT_MOD_INVALID) {
        ++m_modifiersCount;
        m_modifiers = static_cast<uint64_t*>(realloc(m_modifiers, m_modifiersCount * sizeof(*m_modifiers)));
        m_modifiers[m_modifiersCount - 1] = modifier;
    }
}

void Wayland::registerInterface(struct wl_registry* registry, uint32_t id, const char* interface, uint32_t version)
{
    if (!strcmp(interface, wl_compositor_interface.name)) {
        Logger::info("Registering interface (%s) ...\n", interface);
        m_wlCompositor = static_cast<struct wl_compositor*>(wl_registry_bind(registry, id, &wl_compositor_interface, 1));
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        Logger::info("Registering interface (%s) ...\n", interface);
        m_xdgWmBase = static_cast<struct xdg_wm_base*>(wl_registry_bind(registry, id, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(m_xdgWmBase, &xdg_wm_base_listener, nullptr);
    } else if (!strcmp(interface, "zwp_linux_dmabuf_v1")) {
        if (version < 3) {
            Logger::error("Failed to register interface (%s), version: %i < 3.\n", interface, version);
            return;
        }
        Logger::info("Registering interface (%s) ...\n", interface);
        m_zwpLinuxDmabufV1 = static_cast<struct zwp_linux_dmabuf_v1*>(wl_registry_bind(registry, id, &zwp_linux_dmabuf_v1_interface, 3));
        zwp_linux_dmabuf_v1_add_listener(m_zwpLinuxDmabufV1, &dmabuf_listener, this);
    } else if (!strcmp(interface, "zwp_linux_explicit_synchronization_v1")) {
        Logger::info("Registering interface (%s) ...\n", interface);
        m_zwpLinuxExplicitSynchronizationV1 = static_cast<struct zwp_linux_explicit_synchronization_v1*>(wl_registry_bind(registry, id, &zwp_linux_explicit_synchronization_v1_interface, 1));
    }
}

std::unique_ptr<Wayland> Wayland::create(const DRM& drm, const GBM& gbm)
{
    Logger::info("Initializing Wayland...\n");

    auto* display = wl_display_connect(nullptr);
    if (!display) {
        Logger::error("Could not open Wayland display\n");
        return nullptr;
    }

    return std::make_unique<Wayland>(display, drm, gbm);
}
