#include "gl_api.hpp"

#include <dlfcn.h>

// GL and GLX entry points resolved at run time. The injected library cannot
// link libGL, and the same table serves processes that own their context
// (capture overlay, preview), where RTLD_NEXT reaches the linked libGL.
namespace glyphshift::opengl {
const GlxApi& glx() {
    static const GlxApi value = [] {
        GlxApi result;
        result.context =
            reinterpret_cast<decltype(result.context)>(dlsym(RTLD_NEXT, "glXGetCurrentContext"));
        result.drawable =
            reinterpret_cast<decltype(result.drawable)>(dlsym(RTLD_NEXT, "glXGetCurrentDrawable"));
        result.display =
            reinterpret_cast<decltype(result.display)>(dlsym(RTLD_NEXT, "glXGetCurrentDisplay"));
        result.query =
            reinterpret_cast<decltype(result.query)>(dlsym(RTLD_NEXT, "glXQueryDrawable"));
        return result;
    }();
    return value;
}

const GlApi& api() {
    static const GlApi value = [] {
        GlApi result;
        using GetProc = void (*(*)(const GLubyte*))();
        auto get = reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddressARB"));
        if (!get)
            get = reinterpret_cast<GetProc>(dlsym(RTLD_NEXT, "glXGetProcAddress"));
        if (!get)
            return result;
        result.ViewportIndexedf = reinterpret_cast<decltype(result.ViewportIndexedf)>(
            get(reinterpret_cast<const GLubyte*>("glViewportIndexedf")));
        result.ScissorIndexed = reinterpret_cast<decltype(result.ScissorIndexed)>(
            get(reinterpret_cast<const GLubyte*>("glScissorIndexed")));
        result.ClipControl = reinterpret_cast<decltype(result.ClipControl)>(
            get(reinterpret_cast<const GLubyte*>("glClipControl")));
        result.BlendFuncSeparatei = reinterpret_cast<decltype(result.BlendFuncSeparatei)>(
            get(reinterpret_cast<const GLubyte*>("glBlendFuncSeparatei")));
        result.BlendEquationSeparatei = reinterpret_cast<decltype(result.BlendEquationSeparatei)>(
            get(reinterpret_cast<const GLubyte*>("glBlendEquationSeparatei")));
        result.GenQueries = reinterpret_cast<decltype(result.GenQueries)>(
            get(reinterpret_cast<const GLubyte*>("glGenQueries")));
        result.DeleteQueries = reinterpret_cast<decltype(result.DeleteQueries)>(
            get(reinterpret_cast<const GLubyte*>("glDeleteQueries")));
        result.QueryCounter = reinterpret_cast<decltype(result.QueryCounter)>(
            get(reinterpret_cast<const GLubyte*>("glQueryCounter")));
        result.GetQueryObjectiv = reinterpret_cast<decltype(result.GetQueryObjectiv)>(
            get(reinterpret_cast<const GLubyte*>("glGetQueryObjectiv")));
        result.GetQueryObjectui64v = reinterpret_cast<decltype(result.GetQueryObjectui64v)>(
            get(reinterpret_cast<const GLubyte*>("glGetQueryObjectui64v")));
        result.available = true;
#define LOAD(member, name)                                                                       \
    result.member =                                                                              \
        reinterpret_cast<decltype(result.member)>(get(reinterpret_cast<const GLubyte*>(#name))); \
    result.available &= result.member != nullptr;
        GLYPHSHIFT_GL_FUNCTIONS(LOAD)
#undef LOAD
        return result;
    }();
    return value;
}
}  // namespace glyphshift::opengl
