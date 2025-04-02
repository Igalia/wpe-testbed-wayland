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

#include "DMABuffer.h"

#include "Application.h"
#include "DRM.h"
#include "EGL.h"
#include "GBM.h"
#include "Logger.h"

#include <cassert>
#include <cstdlib>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#include <xf86drm.h>

#ifndef HAVE_GBM_BO_GET_FD_FOR_PLANE
static inline int gbm_bo_get_fd_for_plane(struct gbm_bo* bo, int plane)
{
    auto handle = gbm_bo_get_handle_for_plane(bo, plane);
    if (handle.s32 == -1)
        return -1;

    int fd = -1;
    if (drmPrimeHandleToFD(gbm_device_get_fd(gbm_bo_get_device(bo)), handle.u32, DRM_CLOEXEC, &fd) < 0)
        return -1;

    return fd;
}
#endif

DMABuffer::DMABuffer(Role role, const EGL& egl, uint32_t format, uint32_t width, uint32_t height)
    : m_role(role)
    , m_egl(egl)
    , m_format(format)
    , m_width(width)
    , m_height(height)
{
    Logger::info("Create DMABuffer() using role %i format %i width %i height %i\n", role, format, width, height);
}

DMABuffer::~DMABuffer()
{
    if (m_glFrameBuffer) {
        glDeleteFramebuffers(1, &m_glFrameBuffer);
        m_glFrameBuffer = 0;
    }

    if (m_glColorBuffer)
        glDeleteRenderbuffers(1, &m_glColorBuffer);

    if (m_glDepthStencilBuffer)
        glDeleteRenderbuffers(1, &m_glDepthStencilBuffer);

    if (m_glTexture) {
        glDeleteTextures(1, &m_glTexture);
        m_glTexture = 0;
    }

    if (m_eglImage) {
        m_egl.eglDestroyImageKHR(m_egl.display(), m_eglImage);
        m_eglImage = 0;
    }

    /* FIXME
    if (buf->buffer) {
        wl_buffer_destroy(buf->buffer);
        buf->buffer = nullptr;
    }
    */

    if (m_gbmBufferObject) {
        gbm_bo_destroy(m_gbmBufferObject);
        m_gbmBufferObject = nullptr;
    }

    for (uint32_t i = 0; i < m_planeCount; ++i) {
        if (m_dmabufFD[i] >= 0) {
            close(m_dmabufFD[i]);
            m_dmabufFD[i] = -1;
        }
    }
}

std::unique_ptr<DMABuffer> DMABuffer::create(Role role, const DRM& drm, const GBM& gbm, const EGL& egl, uint32_t format, uint32_t width, uint32_t height)
{
    auto dmaBuffer = std::make_unique<DMABuffer>(role, egl, format, width, height);
    if (!dmaBuffer->allocateBufferObject(drm, gbm))
        return nullptr;
    if (!dmaBuffer->createGLFrameBuffer())
        return nullptr;
    return dmaBuffer;
}

inline uint64_t bufferModifierToDRMModifier(const BufferModifier& bufferModifier)
{
    switch (bufferModifier) {
    case BufferModifier::Linear:
        return DRM_FORMAT_MOD_LINEAR;
    case BufferModifier::VivanteTiled:
        return DRM_FORMAT_MOD_VIVANTE_TILED;
    case BufferModifier::VivanteSuperTiled:
        return DRM_FORMAT_MOD_VIVANTE_SUPER_TILED;
    }

    abort();
    return DRM_FORMAT_MOD_LINEAR;
}

bool DMABuffer::allocateBufferObject(const DRM& drm, const GBM& gbm)
{
fprintf(stderr, "1\n");
    auto& args = Application::commandLineArguments();

    uint32_t flags = GBM_BO_USE_RENDERING;

    uint32_t modifiersCount = 1;
    uint64_t* modifiers = static_cast<uint64_t*>(malloc(sizeof(uint64_t)));
    if (m_role == Role::WindowBuffer) {
        *modifiers = bufferModifierToDRMModifier(args.windowBufferModifier);
        flags |= GBM_BO_USE_SCANOUT;
    } else
        *modifiers = bufferModifierToDRMModifier(args.tileBufferModifier);

fprintf(stderr, "2\n");
    if (modifiersCount > 0) {
#ifdef HAVE_GBM_BO_CREATE_WITH_MODIFIERS2
        m_gbmBufferObject = gbm_bo_create_with_modifiers2(gbm.device(), m_width, m_height, m_format, modifiers, modifiersCount, flags);
fprintf(stderr, "3\n");
#else
        (void)flags;
        m_gbmBufferObject = gbm_bo_create_with_modifiers(gbm.device(), m_width, m_height, m_format, modifiers, modifiersCount);
fprintf(stderr, "4\n");
#endif
        if (m_gbmBufferObject)
            m_modifier = gbm_bo_get_modifier(m_gbmBufferObject);
        free(modifiers);
    }

    // Fallback.
    if (!m_gbmBufferObject) {
fprintf(stderr, "5\n");
        m_gbmBufferObject = gbm_bo_create(gbm.device(), m_width, m_height, m_format, flags | GBM_BO_USE_LINEAR);
        m_modifier = DRM_FORMAT_MOD_INVALID;
    }

    if (!m_gbmBufferObject)
        return false;

fprintf(stderr, "6\n");
    m_planeCount = gbm_bo_get_plane_count(m_gbmBufferObject);
    for (uint32_t i = 0; i < m_planeCount; ++i) {
        if (args.tileUpdateMethod == TileUpdateMethod::GLTexSubImage2D)
            m_dmabufFD[i] = gbm_bo_get_fd_for_plane(m_gbmBufferObject, i);
        else {
            const uint32_t handle = gbm_bo_get_handle(m_gbmBufferObject).u32;
            drmPrimeHandleToFD(drm.fd(), handle, DRM_RDWR | DRM_CLOEXEC, &m_dmabufFD[i]);
        }

        m_strides[i] = gbm_bo_get_stride_for_plane(m_gbmBufferObject, i);
        m_offsets[i] = gbm_bo_get_offset(m_gbmBufferObject, i);
    }

fprintf(stderr, "7\n");
    return true;
}

bool DMABuffer::createGLFrameBuffer()
{
    static constexpr uint32_t generalAttributes = 3;
    static constexpr uint32_t planeAttributes = 5;
    static constexpr uint32_t entriesPerAttribute = 2;

    EGLint eglAttributes[(generalAttributes + planeAttributes * maxBufferPlanes) * entriesPerAttribute + 1];

    uint32_t attributeIndex = 0;
    eglAttributes[attributeIndex++] = EGL_WIDTH;
    eglAttributes[attributeIndex++] = m_width;
    eglAttributes[attributeIndex++] = EGL_HEIGHT;
    eglAttributes[attributeIndex++] = m_height;
    eglAttributes[attributeIndex++] = EGL_LINUX_DRM_FOURCC_EXT;
    eglAttributes[attributeIndex++] = m_format;

#define ADD_PLANE_ATTRIBS(planeIndex)                                                          \
    {                                                                                          \
        eglAttributes[attributeIndex++] = EGL_DMA_BUF_PLANE##planeIndex##_FD_EXT;              \
        eglAttributes[attributeIndex++] = m_dmabufFD[planeIndex];                              \
        eglAttributes[attributeIndex++] = EGL_DMA_BUF_PLANE##planeIndex##_OFFSET_EXT;          \
        eglAttributes[attributeIndex++] = static_cast<int32_t>(m_offsets[planeIndex]);         \
        eglAttributes[attributeIndex++] = EGL_DMA_BUF_PLANE##planeIndex##_PITCH_EXT;           \
        eglAttributes[attributeIndex++] = static_cast<int32_t>(m_strides[planeIndex]);         \
        if (m_modifier != DRM_FORMAT_MOD_INVALID) {                                            \
            eglAttributes[attributeIndex++] = EGL_DMA_BUF_PLANE##planeIndex##_MODIFIER_LO_EXT; \
            eglAttributes[attributeIndex++] = m_modifier & 0xFFFFFFFF;                         \
            eglAttributes[attributeIndex++] = EGL_DMA_BUF_PLANE##planeIndex##_MODIFIER_HI_EXT; \
            eglAttributes[attributeIndex++] = m_modifier >> 32;                                \
        }                                                                                      \
    }

    if (m_planeCount > 0)
        ADD_PLANE_ATTRIBS(0);

    if (m_planeCount > 1)
        ADD_PLANE_ATTRIBS(1);

    if (m_planeCount > 2)
        ADD_PLANE_ATTRIBS(2);

    if (m_planeCount > 3)
        ADD_PLANE_ATTRIBS(3);
#undef ADD_PLANE_ATTRIBS

    eglAttributes[attributeIndex] = EGL_NONE;
    assert(attributeIndex < (sizeof(eglAttributes) / sizeof(eglAttributes)[0]));

fprintf(stderr, "8\n");
    m_eglImage = m_egl.eglCreateImageKHR(m_egl.display(), EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, eglAttributes);
    if (m_eglImage == EGL_NO_IMAGE_KHR) {
        Logger::error("EGLImageKHR creation failed\n");
        return false;
    }

fprintf(stderr, "9\n");
    eglMakeCurrent(m_egl.display(), EGL_NO_SURFACE, EGL_NO_SURFACE, m_egl.context());

    const bool isTile = m_format == DRM_FORMAT_ABGR8888;

    // skip FBO creation for tiles or in rbo mode
    auto& args = Application::commandLineArguments();
    if (!args.rbo || isTile) {
fprintf(stderr, "10\n");
        glGenTextures(1, &m_glTexture);
        glBindTexture(GL_TEXTURE_2D, m_glTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, args.linearFilter ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, args.linearFilter ? GL_LINEAR : GL_NEAREST);
fprintf(stderr, "11\n");
        m_egl.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, m_eglImage);
fprintf(stderr, "12\n");
    }

    if (isTile)
        return true;

fprintf(stderr, "13\n");
    if (args.rbo) {
        glGenRenderbuffers(1, &m_glColorBuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_glColorBuffer);
        m_egl.glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, m_eglImage);
    }

    glGenFramebuffers(1, &m_glFrameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_glFrameBuffer);

    glGenRenderbuffers(1, &m_glDepthStencilBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_glDepthStencilBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, m_width, m_height);
fprintf(stderr, "14\n");

    if (args.rbo)
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_glColorBuffer);
    else
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_glTexture, 0);

fprintf(stderr, "15\n");
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_glDepthStencilBuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_glDepthStencilBuffer);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        Logger::error("FBO creation failed\n");
        return false;
    }

    return true;
}
