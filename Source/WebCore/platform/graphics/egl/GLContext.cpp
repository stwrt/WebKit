/*
 * Copyright (C) 2012,2023 Igalia, S.L.
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "config.h"
#include "GLContext.h"

#include "GraphicsContextGL.h"
#include "Logging.h"
#include <wtf/TZoneMallocInlines.h>
#include <wtf/Vector.h>
#include <wtf/text/StringToIntegerConversion.h>

#if USE(LIBEPOXY)
#include <epoxy/egl.h>
#include <epoxy/gl.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#endif

namespace WebCore {

WTF_MAKE_TZONE_ALLOCATED_IMPL(GLContext);

const char* GLContext::errorString(int statusCode)
{
    static_assert(sizeof(int) >= sizeof(EGLint), "EGLint must not be wider than int");
    switch (statusCode) {
#define CASE_RETURN_STRING(name) case name: return #name
        // https://www.khronos.org/registry/EGL/sdk/docs/man/html/eglGetError.xhtml
        CASE_RETURN_STRING(EGL_SUCCESS);
        CASE_RETURN_STRING(EGL_NOT_INITIALIZED);
        CASE_RETURN_STRING(EGL_BAD_ACCESS);
        CASE_RETURN_STRING(EGL_BAD_ALLOC);
        CASE_RETURN_STRING(EGL_BAD_ATTRIBUTE);
        CASE_RETURN_STRING(EGL_BAD_CONTEXT);
        CASE_RETURN_STRING(EGL_BAD_CONFIG);
        CASE_RETURN_STRING(EGL_BAD_CURRENT_SURFACE);
        CASE_RETURN_STRING(EGL_BAD_DISPLAY);
        CASE_RETURN_STRING(EGL_BAD_SURFACE);
        CASE_RETURN_STRING(EGL_BAD_MATCH);
        CASE_RETURN_STRING(EGL_BAD_PARAMETER);
        CASE_RETURN_STRING(EGL_BAD_NATIVE_PIXMAP);
        CASE_RETURN_STRING(EGL_BAD_NATIVE_WINDOW);
        CASE_RETURN_STRING(EGL_CONTEXT_LOST);
#undef CASE_RETURN_STRING
    default: return "Unknown EGL error";
    }
}

const char* GLContext::lastErrorString()
{
    return errorString(eglGetError());
}

bool GLContext::getEGLConfig(PlatformDisplay& platformDisplay, EGLConfig* config, EGLSurfaceType surfaceType)
{
    std::array<EGLint, 4> rgbaSize = { 8, 8, 8, 8 };
    if (const char* environmentVariable = getenv("WEBKIT_EGL_PIXEL_LAYOUT")) {
        if (!strcmp(environmentVariable, "RGB565"))
            rgbaSize = { 5, 6, 5, 0 };
        else
            WTFLogAlways("Unknown pixel layout %s, falling back to RGBA8888", environmentVariable);
    }

    WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GLib / Windows ports.
    EGLint attributeList[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, rgbaSize[0],
        EGL_GREEN_SIZE, rgbaSize[1],
        EGL_BLUE_SIZE, rgbaSize[2],
        EGL_ALPHA_SIZE, rgbaSize[3],
        EGL_STENCIL_SIZE, 8,
        EGL_SURFACE_TYPE, EGL_NONE,
        EGL_DEPTH_SIZE, 0,
        EGL_NONE
    };
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_END

    switch (surfaceType) {
    case GLContext::Surfaceless:
        if (platformDisplay.type() == PlatformDisplay::Type::Surfaceless)
            attributeList[13] = EGL_PBUFFER_BIT;
        else
            attributeList[13] = EGL_WINDOW_BIT;
        break;
    case GLContext::PbufferSurface:
        attributeList[13] = EGL_PBUFFER_BIT;
        break;
    case GLContext::PixmapSurface:
        attributeList[13] = EGL_PIXMAP_BIT;
        break;
    case GLContext::WindowSurface:
        attributeList[13] = EGL_WINDOW_BIT;
        break;
    }

    EGLDisplay display = platformDisplay.eglDisplay();
    EGLint count;
    if (!eglChooseConfig(display, attributeList, nullptr, 0, &count)) {
        RELEASE_LOG_INFO(Compositing, "Cannot get count of available EGL configurations: %s.", lastErrorString());
        return false;
    }

    EGLint numberConfigsReturned;
    Vector<EGLConfig> configs(count);
    if (!eglChooseConfig(display, attributeList, reinterpret_cast<EGLConfig*>(configs.mutableSpan().data()), count, &numberConfigsReturned) || !numberConfigsReturned) {
        RELEASE_LOG_INFO(Compositing, "Cannot get available EGL configurations: %s.", lastErrorString());
        return false;
    }

    auto index = configs.findIf([&](EGLConfig value) {
        EGLint redSize, greenSize, blueSize, alphaSize;
        eglGetConfigAttrib(display, value, EGL_RED_SIZE, &redSize);
        eglGetConfigAttrib(display, value, EGL_GREEN_SIZE, &greenSize);
        eglGetConfigAttrib(display, value, EGL_BLUE_SIZE, &blueSize);
        eglGetConfigAttrib(display, value, EGL_ALPHA_SIZE, &alphaSize);
        return redSize == rgbaSize[0] && greenSize == rgbaSize[1]
            && blueSize == rgbaSize[2] && alphaSize == rgbaSize[3];
    });

    if (index != notFound) {
        *config = configs[index];
        return true;
    }

    RELEASE_LOG_INFO(Compositing, "Could not find suitable EGL configuration out of %zu checked.", configs.size());
    return false;
}

std::unique_ptr<GLContext> GLContext::createWindowContext(GLNativeWindowType window, PlatformDisplay& platformDisplay, EGLContext sharingContext)
{
    EGLConfig config;
    if (!getEGLConfig(platformDisplay, &config, WindowSurface)) {
        RELEASE_LOG_INFO(Compositing, "Cannot obtain EGL window context configuration: %s\n", lastErrorString());
        return nullptr;
    }

    EGLContext context = createContextForEGLVersion(platformDisplay, config, sharingContext);
    if (context == EGL_NO_CONTEXT) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL window context: %s\n", lastErrorString());
        return nullptr;
    }

    EGLDisplay display = platformDisplay.eglDisplay();
    EGLSurface surface = EGL_NO_SURFACE;
    switch (platformDisplay.type()) {
#if USE(WPE_RENDERER)
    case PlatformDisplay::Type::WPE:
        surface = createWindowSurfaceWPE(display, config, window);
        break;
#endif // USE(WPE_RENDERER)
#if USE(GBM)
    case PlatformDisplay::Type::GBM:
#endif
#if PLATFORM(GTK)
    case PlatformDisplay::Type::Default:
#endif
    case PlatformDisplay::Type::Surfaceless:
        RELEASE_ASSERT_NOT_REACHED();
        break;
    default:
        break;
    }

    if (surface == EGL_NO_SURFACE) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL window surface: %s. Retrying with fallback.", lastErrorString());
        // EGLNativeWindowType changes depending on the EGL implementation, reinterpret_cast
        // would work for pointers, and static_cast for numeric types only; so use a plain
        // C cast expression which works in all possible cases.
        surface = eglCreateWindowSurface(display, config, (EGLNativeWindowType) window, nullptr);
    }

    if (surface == EGL_NO_SURFACE) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL window surface: %s\n", lastErrorString());
        eglDestroyContext(display, context);
        return nullptr;
    }

    return makeUnique<GLContext>(platformDisplay, context, surface, config, WindowSurface);
}

std::unique_ptr<GLContext> GLContext::createPbufferContext(PlatformDisplay& platformDisplay, EGLContext sharingContext)
{
    EGLConfig config;
    if (!getEGLConfig(platformDisplay, &config, PbufferSurface)) {
        RELEASE_LOG_INFO(Compositing, "Cannot obtain EGL Pbuffer configuration: %s\n", lastErrorString());
        return nullptr;
    }

    EGLContext context = createContextForEGLVersion(platformDisplay, config, sharingContext);
    if (context == EGL_NO_CONTEXT) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL Pbuffer context: %s\n", lastErrorString());
        return nullptr;
    }

    EGLDisplay display = platformDisplay.eglDisplay();
    static const int pbufferAttributes[] = { EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE };
    EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttributes);
    if (surface == EGL_NO_SURFACE) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL Pbuffer surface: %s\n", lastErrorString());
        eglDestroyContext(display, context);
        return nullptr;
    }

    return makeUnique<GLContext>(platformDisplay, context, surface, config, PbufferSurface);
}

std::unique_ptr<GLContext> GLContext::createSurfacelessContext(PlatformDisplay& platformDisplay, EGLContext sharingContext)
{
    EGLDisplay display = platformDisplay.eglDisplay();
    if (display == EGL_NO_DISPLAY) {
        RELEASE_LOG_INFO(Compositing, "Cannot create surfaceless EGL context: invalid display (last error: %s)\n", lastErrorString());
        return nullptr;
    }

    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    if (!GLContext::isExtensionSupported(extensions, "EGL_KHR_surfaceless_context") && !GLContext::isExtensionSupported(extensions, "EGL_KHR_surfaceless_opengl")) {
        RELEASE_LOG_INFO(Compositing, "Cannot create surfaceless EGL context: required extensions missing.");
        return nullptr;
    }

    EGLConfig config;
    if (!getEGLConfig(platformDisplay, &config, Surfaceless)) {
        RELEASE_LOG_INFO(Compositing, "Cannot obtain EGL surfaceless configuration: %s\n", lastErrorString());
        return nullptr;
    }

    EGLContext context = createContextForEGLVersion(platformDisplay, config, sharingContext);
    if (context == EGL_NO_CONTEXT) {
        RELEASE_LOG_INFO(Compositing, "Cannot create EGL surfaceless context: %s\n", lastErrorString());
        return nullptr;
    }

    return makeUnique<GLContext>(platformDisplay, context, EGL_NO_SURFACE, config, Surfaceless);
}

std::unique_ptr<GLContext> GLContext::create(GLNativeWindowType window, PlatformDisplay& platformDisplay)
{
    if (!window)
        return GLContext::createOffscreen(platformDisplay);

    if (platformDisplay.eglDisplay() == EGL_NO_DISPLAY) {
        WTFLogAlways("Cannot create EGL context: invalid display (last error: %s)\n", lastErrorString());
        return nullptr;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        WTFLogAlways("Cannot create EGL context: error binding OpenGL ES API (%s)\n", lastErrorString());
        return nullptr;
    }

    EGLContext eglSharingContext = platformDisplay.sharingGLContext() ? static_cast<GLContext*>(platformDisplay.sharingGLContext())->m_context : EGL_NO_CONTEXT;
    auto context = createWindowContext(window, platformDisplay, eglSharingContext);
    if (!context)
        WTFLogAlways("Could not create EGL context.");
    return context;
}

std::unique_ptr<GLContext> GLContext::createOffscreen(PlatformDisplay& platformDisplay)
{
    if (platformDisplay.eglDisplay() == EGL_NO_DISPLAY) {
        WTFLogAlways("Cannot create EGL context: invalid display (last error: %s)\n", lastErrorString());
        return nullptr;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        WTFLogAlways("Cannot create EGL context: error binding OpenGL ES API (%s)\n", lastErrorString());
        return nullptr;
    }

    EGLContext eglSharingContext = platformDisplay.sharingGLContext() ? static_cast<GLContext*>(platformDisplay.sharingGLContext())->m_context : EGL_NO_CONTEXT;
    auto context = createSurfacelessContext(platformDisplay, eglSharingContext);
    if (!context) {
        switch (platformDisplay.type()) {
#if USE(WPE_RENDERER)
        case PlatformDisplay::Type::WPE:
            context = createWPEContext(platformDisplay, eglSharingContext);
            break;
#endif
#if USE(GBM)
        case PlatformDisplay::Type::GBM:
#endif
        case PlatformDisplay::Type::Surfaceless:
            // Do not fallback to pbuffers.
            WTFLogAlways("Could not create EGL surfaceless context: %s.", lastErrorString());
            return nullptr;
        default:
            break;
        }
    }
    if (!context) {
        RELEASE_LOG_INFO(Compositing, "Could not create platform context: %s. Using Pbuffer as fallback.", lastErrorString());
        context = createPbufferContext(platformDisplay, eglSharingContext);
        if (!context)
            RELEASE_LOG_INFO(Compositing, "Could not create Pbuffer context: %s.", lastErrorString());
    }

    return context;
}

std::unique_ptr<GLContext> GLContext::createSharing(PlatformDisplay& platformDisplay)
{
    if (platformDisplay.eglDisplay() == EGL_NO_DISPLAY) {
        WTFLogAlways("Cannot create EGL sharing context: invalid display (last error: %s)", lastErrorString());
        return nullptr;
    }

    if (eglBindAPI(EGL_OPENGL_ES_API) == EGL_FALSE) {
        WTFLogAlways("Cannot create EGL sharing context: error binding OpenGL ES API (%s)\n", lastErrorString());
        return nullptr;
    }

    auto context = createSurfacelessContext(platformDisplay);
    if (!context) {
        switch (platformDisplay.type()) {
#if USE(WPE_RENDERER)
        case PlatformDisplay::Type::WPE:
            context = createWPEContext(platformDisplay);
            break;
#endif
#if USE(GBM)
        case PlatformDisplay::Type::GBM:
#endif
        case PlatformDisplay::Type::Surfaceless:
            // Do not fallback to pbuffers.
            WTFLogAlways("Could not create EGL surfaceless context: %s.", lastErrorString());
            return nullptr;
        default:
            break;
        }
    }
    if (!context) {
        RELEASE_LOG_INFO(Compositing, "Could not create platform context: %s. Using Pbuffer as fallback.", lastErrorString());
        context = createPbufferContext(platformDisplay);
        if (!context)
            RELEASE_LOG_INFO(Compositing, "Could not create Pbuffer context: %s.", lastErrorString());
    }

    if (!context)
        WTFLogAlways("Could not create EGL sharing context.");
    return context;
}

GLContext::GLContext(PlatformDisplay& display, EGLContext context, EGLSurface surface, EGLConfig config, EGLSurfaceType type)
    : m_display(display)
    , m_context(context)
    , m_surface(surface)
    , m_config(config)
    , m_type(type)
{
    ASSERT(type != PixmapSurface);
    ASSERT(type == Surfaceless || surface != EGL_NO_SURFACE);
    RELEASE_ASSERT(m_display.eglDisplay() != EGL_NO_DISPLAY);
    RELEASE_ASSERT(context != EGL_NO_CONTEXT);

#if ENABLE(MEDIA_TELEMETRY)
    if (m_type == WindowSurface) {
        MediaTelemetryReport::singleton().reportWaylandInfo(*this, MediaTelemetryReport::WaylandAction::InitGfx,
            MediaTelemetryReport::WaylandGraphicsState::GfxInitialized, MediaTelemetryReport::WaylandInputsState::InputsInitialized);
    }
#endif
}

GLContext::~GLContext()
{
    EGLDisplay display = m_display.eglDisplay();
    if (m_context) {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(display, m_context);
    }

    if (m_surface)
        eglDestroySurface(display, m_surface);

#if USE(WPE_RENDERER)
    destroyWPETarget();
#endif

#if ENABLE(MEDIA_TELEMETRY)
    if (m_type == WindowSurface) {
        MediaTelemetryReport::singleton().reportWaylandInfo(*this, MediaTelemetryReport::WaylandAction::DeinitGfx,
            MediaTelemetryReport::WaylandGraphicsState::GfxNotInitialized, MediaTelemetryReport::WaylandInputsState::InputsInitialized);
    }
#endif
}

EGLContext GLContext::createContextForEGLVersion(PlatformDisplay& platformDisplay, EGLConfig config, EGLContext sharingContext)
{
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GLib / Windows ports.
    static EGLint contextAttributes[3];
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
    static bool contextAttributesInitialized = false;

    if (!contextAttributesInitialized) {
        contextAttributesInitialized = true;

        contextAttributes[0] = EGL_CONTEXT_CLIENT_VERSION;
        contextAttributes[1] = 2;
        contextAttributes[2] = EGL_NONE;
    }

    return eglCreateContext(platformDisplay.eglDisplay(), config, sharingContext, contextAttributes);
}

bool GLContext::makeCurrentImpl()
{
    ASSERT(m_context);
    return eglMakeCurrent(m_display.eglDisplay(), m_surface, m_surface, m_context);
}

bool GLContext::unmakeCurrentImpl()
{
    return eglMakeCurrent(m_display.eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

bool GLContext::makeContextCurrent()
{
    if (isCurrent())
        return true;

    // ANGLE doesn't know anything about non-ANGLE contexts, and does
    // nothing in MakeCurrent if what it thinks is current hasn't changed.
    // So, when making a native context current we need to unmark any previous
    // ANGLE context to ensure the next MakeCurrent does the right thing.
    auto* context = currentContext();
    bool isSwitchingFromANGLE = context && context->type() == GLContextWrapper::Type::Angle;
    if (isSwitchingFromANGLE)
        context->unmakeCurrentImpl();

    if (eglMakeCurrent(m_display.eglDisplay(), m_surface, m_surface, m_context)) {
        didMakeContextCurrent();
        return true;
    }

    // If we failed to make the native context current, restore the previous ANGLE one.
    if (isSwitchingFromANGLE)
        context->makeCurrentImpl();

    return false;
}

bool GLContext::unmakeContextCurrent()
{
    if (!isCurrent())
        return true;

    if (eglMakeCurrent(m_display.eglDisplay(), EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
        didUnmakeContextCurrent();
        return true;
    }

    return false;
}

GLContext* GLContext::current()
{
    auto* context = currentContext();
    if (context && context->type() == GLContextWrapper::Type::Native)
        return static_cast<GLContext*>(context);
    return nullptr;
}

void GLContext::swapBuffers()
{
    if (m_type == Surfaceless)
        return;

    ASSERT(m_surface);
    eglSwapBuffers(m_display.eglDisplay(), m_surface);
}

GCGLContext GLContext::platformContext() const
{
    return m_context;
}

bool GLContext::isExtensionSupported(const char* extensionList, const char* extension)
{
    if (!extensionList)
        return false;

    ASSERT(extension);
    int extensionLen = strlen(extension);
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_BEGIN // GLib / Windows ports.
    const char* extensionListPtr = extensionList;
    WTF_ALLOW_UNSAFE_BUFFER_USAGE_END
    while ((extensionListPtr = strstr(extensionListPtr, extension))) {
        if (extensionListPtr[extensionLen] == ' ' || extensionListPtr[extensionLen] == '\0')
            return true;
        extensionListPtr += extensionLen;
    }
    return false;
}

unsigned GLContext::versionFromString(const char* versionStringAsChar)
{
    auto versionString = String::fromLatin1(versionStringAsChar);
    Vector<String> versionStringComponents = versionString.split(' ');

    Vector<String> versionDigits;
    if (versionStringComponents[0] == "OpenGL"_s) {
        // If the version string starts with "OpenGL" it can be GLES 1 or 2. In GLES1 version string starts
        // with "OpenGL ES-<profile> major.minor" and in GLES2 with "OpenGL ES major.minor". Version is the
        // third component in both cases.
        versionDigits = versionStringComponents[2].split('.');
    } else {
        // Version is the first component. The version number is always "major.minor" or
        // "major.minor.release". Ignore the release number.
        versionDigits = versionStringComponents[0].split('.');
    }

    return parseIntegerAllowingTrailingJunk<unsigned>(versionDigits[0]).value_or(0) * 100 + parseIntegerAllowingTrailingJunk<unsigned>(versionDigits[1]).value_or(0) * 10;
}

unsigned GLContext::version()
{
    if (!m_version) {
        auto* versionString = reinterpret_cast<const char*>(::glGetString(GL_VERSION));
        m_version = versionFromString(versionString);
    }

    return m_version;
}

const GLContext::GLExtensions& GLContext::glExtensions() const
{
    static std::once_flag flag;
    std::call_once(flag, [this] {
        const char* extensionsString = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        m_glExtensions.OES_texture_npot = isExtensionSupported(extensionsString, "GL_OES_texture_npot");
        m_glExtensions.EXT_unpack_subimage = isExtensionSupported(extensionsString, "GL_EXT_unpack_subimage");
        m_glExtensions.APPLE_sync = isExtensionSupported(extensionsString, "GL_APPLE_sync");
        m_glExtensions.OES_packed_depth_stencil = isExtensionSupported(extensionsString, "GL_OES_packed_depth_stencil");
    });
    return m_glExtensions;
}

GLContext::ScopedGLContext::ScopedGLContext(std::unique_ptr<GLContext>&& context)
    : m_context(WTFMove(context))
{
    auto eglContext = eglGetCurrentContext();
    m_previous.glContext = GLContext::current();
    if (!m_previous.glContext || m_previous.glContext->platformContext() != eglContext) {
        m_previous.context = eglContext;
        if (m_previous.context != EGL_NO_CONTEXT) {
            m_previous.display = eglGetCurrentDisplay();
            m_previous.readSurface = eglGetCurrentSurface(EGL_READ);
            m_previous.drawSurface = eglGetCurrentSurface(EGL_DRAW);
        }
    }
    m_context->makeContextCurrent();
}

GLContext::ScopedGLContext::~ScopedGLContext()
{
    m_context = nullptr;

    if (m_previous.context != EGL_NO_CONTEXT)
        eglMakeCurrent(m_previous.display, m_previous.drawSurface, m_previous.readSurface, m_previous.context);
    else if (m_previous.glContext)
        m_previous.glContext->makeContextCurrent();
}

GLContext::ScopedGLContextCurrent::ScopedGLContextCurrent(GLContext& context)
    : m_context(context)
{
    auto eglContext = eglGetCurrentContext();
    m_previous.glContext = GLContext::current();
    if (!m_previous.glContext || m_previous.glContext->platformContext() != eglContext) {
        m_previous.context = eglContext;
        if (m_previous.context != EGL_NO_CONTEXT) {
            m_previous.display = eglGetCurrentDisplay();
            m_previous.readSurface = eglGetCurrentSurface(EGL_READ);
            m_previous.drawSurface = eglGetCurrentSurface(EGL_DRAW);
        }
    }
    m_context.makeContextCurrent();
}

GLContext::ScopedGLContextCurrent::~ScopedGLContextCurrent()
{
    if (m_previous.glContext && m_previous.context == EGL_NO_CONTEXT) {
        m_previous.glContext->makeContextCurrent();
        return;
    }

    m_context.unmakeContextCurrent();

    if (m_previous.context)
        eglMakeCurrent(m_previous.display, m_previous.drawSurface, m_previous.readSurface, m_previous.context);
}

#if ENABLE(MEDIA_TELEMETRY)
EGLDisplay GLContext::eglDisplay() const
{
    return m_display.eglDisplay();
}

EGLConfig GLContext::eglConfig() const
{
    EGLConfig config = nullptr;

    if (!getEGLConfig(m_display, &config, WindowSurface)) {
        WTFLogAlways("Cannot obtain EGL window context configuration: %s\n", lastErrorString());
        config = nullptr;
        ASSERT_NOT_REACHED();
    }

    return config;
}

EGLSurface GLContext::eglSurface() const
{
    return m_surface;
}

EGLContext GLContext::eglContext() const
{
    return m_context;
}

unsigned GLContext::windowWidth() const
{
    return parseInteger<unsigned>(StringView::fromLatin1(std::getenv("WPE_INIT_VIEW_WIDTH"))).value_or(1920);
}

unsigned GLContext::windowHeight() const
{
    return parseInteger<unsigned>(StringView::fromLatin1(std::getenv("WPE_INIT_VIEW_HEIGHT"))).value_or(1080);
}
#endif

} // namespace WebCore
