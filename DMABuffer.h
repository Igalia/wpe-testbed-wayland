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

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GLES2/gl2.h>

class DRM;
class EGL;
class GBM;

class DMABuffer {
public:
    enum class Role {
        TileBuffer,
        WindowBuffer
    };

    DMABuffer(Role, const EGL&, uint32_t format, uint32_t width, uint32_t height);
    ~DMABuffer();

    static std::unique_ptr<DMABuffer> create(Role, const DRM&, const GBM&, const EGL&, uint32_t format, uint32_t width, uint32_t height);

    static constexpr uint32_t maxBufferPlanes = 4;

    struct gbm_bo* gbmBufferObject() const { return m_gbmBufferObject; }
    GLuint glFrameBuffer() const { return m_glFrameBuffer; }
    GLuint glTexture() const { return m_glTexture; }

    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }
    uint32_t format() const { return m_format; }
    uint64_t modifier() const { return m_modifier; }

    uint32_t planeCount() const { return m_planeCount; }
    uint32_t strideForPlane(uint32_t plane) const { return m_strides[plane]; }
    uint32_t offsetForPlane(uint32_t plane) const { return m_offsets[plane]; }
    int32_t dmabufFDForPlane(uint32_t plane) const { return m_dmabufFD[plane]; }

    // Wayland support
    struct wl_buffer* wlBuffer() const { return m_wlBuffer; }
    void setWaylandBuffer(struct wl_buffer* buffer) { m_wlBuffer = buffer; }

    struct zwp_linux_buffer_release_v1* zwpLinuxBufferReleaseV1() const { return m_zwpLinuxBufferReleaseV1; }
    void setBufferRelease(struct zwp_linux_buffer_release_v1* release) { m_zwpLinuxBufferReleaseV1 = release; }

    int32_t releaseFenceFD() const { return m_releaseFenceFD; }
    void setReleaseFenceFD(int32_t fd) { m_releaseFenceFD = fd; }

    bool isInUse() const { return m_isInUse; }
    void setIsInUse(bool isInUse) { m_isInUse = isInUse; }

private:
    bool allocateBufferObject(const DRM&, const GBM&);
    bool createGLFrameBuffer();

    Role m_role { Role::TileBuffer };

    const EGL& m_egl;
    struct gbm_bo* m_gbmBufferObject { nullptr };
    struct wl_buffer* m_wlBuffer { nullptr };
    struct zwp_linux_buffer_release_v1* m_zwpLinuxBufferReleaseV1 { nullptr };

    bool m_isInUse { false };
    int32_t m_releaseFenceFD { -1 };
    uint32_t m_width { 0 };
    uint32_t m_height { 0 };
    uint32_t m_format { 0 };
    uint64_t m_modifier { 0 };
    uint32_t m_planeCount { 0 };
    int32_t m_dmabufFD[maxBufferPlanes] { -1 };
    uint32_t m_strides[maxBufferPlanes] { 0 };
    uint32_t m_offsets[maxBufferPlanes] { 0 };

    EGLImageKHR m_eglImage { nullptr };
    GLuint m_glTexture { 0 };
    GLuint m_glFrameBuffer { 0 };
    GLuint m_glColorBuffer { 0 };
    GLuint m_glDepthStencilBuffer { 0 };
};
