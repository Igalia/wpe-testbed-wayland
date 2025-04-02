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

#include <wayland-client.h>
#include <wayland-egl.h>

class DRM;
class EGL;
class GBM;

struct zwp_linux_dmabuf_v1;

class Wayland {
public:
    Wayland(struct wl_display*, const DRM&, const GBM&);
    ~Wayland();

    static std::unique_ptr<Wayland> create(const DRM&, const GBM&);
    void initializeWithEGL(const EGL&);

    const DRM& drm() const { return m_drm; }
    const GBM& gbm() const { return m_gbm; }
    const EGL& egl() const;

    struct wl_compositor* compositor() const { return m_wlCompositor; }
    struct wl_display* display() const { return m_wlDisplay; }
    struct xdg_wm_base* xdgWmBase() const { return m_xdgWmBase; }
    struct zwp_linux_dmabuf_v1* zwpLinuxDmabufV1() const { return m_zwpLinuxDmabufV1; }
    struct zwp_linux_explicit_synchronization_v1* zwpLinuxExplicitSynchronizationV1() const { return m_zwpLinuxExplicitSynchronizationV1; }

    uint32_t format() const { return m_format; }
    bool useExplicitSync() const { return m_useExplicitSync; }

    void setDMABufModifiers(uint32_t format, uint64_t modifier);
    void registerInterface(struct wl_registry*, uint32_t id, const char* interface, uint32_t version);

private:
    const DRM& m_drm;
    const GBM& m_gbm;
    const EGL* m_egl { nullptr };

    struct wl_display* m_wlDisplay { nullptr };
    struct wl_registry* m_wlRegistry { nullptr };

    struct wl_compositor* m_wlCompositor { nullptr };
    struct xdg_wm_base* m_xdgWmBase { nullptr };
    struct zwp_linux_dmabuf_v1* m_zwpLinuxDmabufV1 { nullptr };
    struct zwp_linux_explicit_synchronization_v1* m_zwpLinuxExplicitSynchronizationV1 { nullptr };

    bool m_initialized : 1 { false };
    bool m_useExplicitSync : 1 { false };
    bool m_formatSupported : 1 { false };
    uint32_t m_format { 0 };
    uint64_t* m_modifiers { nullptr };
    uint32_t m_modifiersCount { 0 };
};
