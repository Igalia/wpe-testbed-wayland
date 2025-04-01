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

#include "EGL.h"

#include "Application.h"
#include "DRM.h"
#include "GBM.h"
#include "Logger.h"

#include <cassert>
#include <cstdio>
#include <cstring>

#include <dlfcn.h>

#if !defined(EGL_EXT_platform_base)
typedef EGLDisplay(EGLAPIENTRYP PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum, void*, const EGLint*);
#endif

EGL::EGL(EGLDisplay&& display)
    : m_display(display)
{
    initialize();

    dumpEGLInformation();
    dumpGLInformation();
}

EGL::~EGL()
{
    assert(m_display != EGL_NO_DISPLAY);
    eglTerminate(m_display);
    eglReleaseThread();
}

void EGL::dumpEGLInformation()
{
    Logger::info("\n===================================\n");
    Logger::info("EGL information:\n");
    Logger::info("  version: \"%s\"\n", eglQueryString(m_display, EGL_VERSION));
    Logger::info("  vendor: \"%s\"\n", eglQueryString(m_display, EGL_VENDOR));
    Logger::info("  client extensions: \"%s\"\n", eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS));
    Logger::info("  display extensions: \"%s\"\n", eglQueryString(m_display, EGL_EXTENSIONS));
    Logger::info("\n");
}

void EGL::dumpGLInformation()
{
    Logger::info("\n===================================\n");
    Logger::info("OpenGL ES 2.x information:\n");
    Logger::info("  version: \"%s\"\n", glGetString(GL_VERSION));
    Logger::info("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    Logger::info("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
    Logger::info("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
    Logger::info("  extensions: \"%s\"\n", glGetString(GL_EXTENSIONS));
    Logger::info("\n");
}

std::unique_ptr<EGL> EGL::create(const GBM& gbm)
{
    static PFNEGLGETPLATFORMDISPLAYEXTPROC s_eglGetPlatformDisplayEXT = nullptr;
    if (!s_eglGetPlatformDisplayEXT)
        s_eglGetPlatformDisplayEXT = reinterpret_cast<decltype(s_eglGetPlatformDisplayEXT)>(eglGetProcAddress("eglGetPlatformDisplayEXT"));

    fprintf(stderr, "GOT eglGetPlatformDisplayEXT %p\n", s_eglGetPlatformDisplayEXT);
    EGLDisplay display;
    if (s_eglGetPlatformDisplayEXT)
        display = s_eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm.device(), nullptr);
    else
        display = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbm.device()));

    if (display == EGL_NO_DISPLAY) {
        Logger::error("Could not open EGL display\n");
        return nullptr;
    }

    EGLint major, minor;
    if (!eglInitialize(display, &major, &minor)) {
        Logger::error("Could not initialize EGL display\n");
        return nullptr;
    }

    return std::make_unique<EGL>(std::move(display));
}

static bool hasEGLExtension(const char* extensionList, const char* extension)
{
    auto* ptr = extensionList;
    if (!ptr || *ptr == '\0')
        return false;

    auto length = strlen(extension);
    while (true) {
        ptr = strstr(ptr, extension);
        if (!ptr)
            return false;

        if (ptr[length] == ' ' || ptr[length] == '\0')
            return true;

        ptr += length;
    }
}

void EGL::initializeExtensions()
{
    const char* displayExtensionString = eglQueryString(m_display, EGL_EXTENSIONS);
    if (hasEGLExtension(displayExtensionString, "EGL_KHR_image_base")) {
        eglCreateImageKHR = reinterpret_cast<decltype(eglCreateImageKHR)>(eglGetProcAddress("eglCreateImageKHR"));
        eglDestroyImageKHR = reinterpret_cast<decltype(eglDestroyImageKHR)>(eglGetProcAddress("eglDestroyImageKHR"));
    }

    if (hasEGLExtension(displayExtensionString, "EGL_KHR_fence_sync")) {
        eglCreateSyncKHR = reinterpret_cast<decltype(eglCreateSyncKHR)>(eglGetProcAddress("eglCreateSyncKHR"));
        eglDestroySyncKHR = reinterpret_cast<decltype(eglDestroySyncKHR)>(eglGetProcAddress("eglDestroySyncKHR"));
        eglWaitSyncKHR = reinterpret_cast<decltype(eglWaitSyncKHR)>(eglGetProcAddress("eglWaitSyncKHR"));
        eglClientWaitSyncKHR = reinterpret_cast<decltype(eglClientWaitSyncKHR)>(eglGetProcAddress("eglClientWaitSyncKHR"));
    }

    if (hasEGLExtension(displayExtensionString, "EGL_ANDROID_native_fence_sync"))
        eglDupNativeFenceFDANDROID = reinterpret_cast<decltype(eglDupNativeFenceFDANDROID)>(eglGetProcAddress("eglDupNativeFenceFDANDROID"));

    const char* glExtensionString = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (hasEGLExtension(glExtensionString, "GL_OES_EGL_image_external")) {
        glEGLImageTargetTexture2DOES = reinterpret_cast<decltype(glEGLImageTargetTexture2DOES)>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
        glEGLImageTargetRenderbufferStorageOES = reinterpret_cast<decltype(glEGLImageTargetRenderbufferStorageOES)>(eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES"));
    }
}

void EGL::initialize()
{
    auto& args = Application::commandLineArguments();

    EGLint numberOfConfig;
    EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, args.opaque ? 0 : 8,
        EGL_NONE
    };

    static const EGLint contextAttributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    EGLConfig configs[1];
    eglChooseConfig(m_display, configAttributes, configs, 1, &numberOfConfig);
    assert(numberOfConfig);

    m_context = eglCreateContext(m_display, configs[0], EGL_NO_CONTEXT, contextAttributes);

    // connect the context to the surface
    EGLBoolean makeCurrent = eglMakeCurrent(m_display, EGL_NO_CONTEXT, EGL_NO_CONTEXT, m_context);
    assert(makeCurrent == EGL_TRUE);

    initializeExtensions();
}

int EGL::createFenceFD() const
{
    EGLSyncKHR fence = eglCreateSyncKHR(m_display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
    assert(fence != EGL_NO_SYNC_KHR);

    // glFlush() is a requirement to retrieve the fence fd
    glFlush();

    int fd = eglDupNativeFenceFDANDROID(m_display, fence);
    assert(fd >= 0);

    eglDestroySyncKHR(m_display, fence);
    return fd;
}

EGLSyncKHR EGL::createFence() const
{
    EGLint attributeList[] = { EGL_NONE };
    auto fence = eglCreateSyncKHR(m_display, EGL_SYNC_FENCE_KHR, attributeList);
    assert(fence != EGL_NO_SYNC_KHR);
    return fence;
}

void EGL::destroyFence(EGLSyncKHR fence) const
{
    eglDestroySyncKHR(m_display, fence);
}

void EGL::clientWaitFence(EGLSyncKHR sync) const
{
    eglClientWaitSyncKHR(m_display, sync, 0, EGL_FOREVER_KHR);
}
