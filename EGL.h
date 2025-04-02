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
#include <GLES2/gl2ext.h>

class GBM;

struct wl_display;

class EGL {
public:
    EGL(EGLDisplay&&);
    ~EGL();

    static std::unique_ptr<EGL> createDefaultPlatform();
    static std::unique_ptr<EGL> createGBMPlatform(const GBM&);

    void initialize();

    EGLDisplay display() const { return m_display; }
    EGLContext context() const { return m_context; }
    bool supportsExplicitSync() const { return eglDupNativeFenceFDANDROID != nullptr; }

    int createFenceFD() const;
    EGLSyncKHR createFence() const;
    void destroyFence(EGLSyncKHR) const;

    void clientWaitFence(EGLSyncKHR) const;

    // Exposed EGL functions
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR { nullptr };
    PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR { nullptr };
    PFNEGLCREATESYNCKHRPROC eglCreateSyncKHR { nullptr };
    PFNEGLDESTROYSYNCKHRPROC eglDestroySyncKHR { nullptr };
    PFNEGLWAITSYNCKHRPROC eglWaitSyncKHR { nullptr };
    PFNEGLCLIENTWAITSYNCKHRPROC eglClientWaitSyncKHR { nullptr };
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC eglDupNativeFenceFDANDROID { nullptr };

    // Exposed GL functions
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES { nullptr };
    PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC glEGLImageTargetRenderbufferStorageOES { nullptr };

private:
    void initializeExtensions();
    void dumpEGLInformation();
    void dumpGLInformation();

    EGLDisplay m_display { EGL_NO_DISPLAY };
    EGLContext m_context { EGL_NO_CONTEXT };
};
