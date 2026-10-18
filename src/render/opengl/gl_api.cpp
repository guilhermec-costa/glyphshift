#include "gl_api.hpp"

#include <mutex>

// GL entry points resolved at run time through the window system's loader:
// the injected library cannot link libGL, and processes that own their
// context (capture overlay, preview) use the same table.
namespace glyphshift::opengl {
namespace {
GlApi table;
std::once_flag loaded;
}  // namespace

const GlApi& load_api(ProcLoader loader) {
    std::call_once(loaded, [loader] {
        if (!loader)
            return;
        GlApi result;
        result.ViewportIndexedf =
            reinterpret_cast<decltype(result.ViewportIndexedf)>(loader("glViewportIndexedf"));
        result.ScissorIndexed =
            reinterpret_cast<decltype(result.ScissorIndexed)>(loader("glScissorIndexed"));
        result.ClipControl =
            reinterpret_cast<decltype(result.ClipControl)>(loader("glClipControl"));
        result.BlendFuncSeparatei =
            reinterpret_cast<decltype(result.BlendFuncSeparatei)>(loader("glBlendFuncSeparatei"));
        result.BlendEquationSeparatei = reinterpret_cast<decltype(result.BlendEquationSeparatei)>(
            loader("glBlendEquationSeparatei"));
        result.GenQueries = reinterpret_cast<decltype(result.GenQueries)>(loader("glGenQueries"));
        result.DeleteQueries =
            reinterpret_cast<decltype(result.DeleteQueries)>(loader("glDeleteQueries"));
        result.QueryCounter =
            reinterpret_cast<decltype(result.QueryCounter)>(loader("glQueryCounter"));
        result.GetQueryObjectiv =
            reinterpret_cast<decltype(result.GetQueryObjectiv)>(loader("glGetQueryObjectiv"));
        result.GetQueryObjectui64v =
            reinterpret_cast<decltype(result.GetQueryObjectui64v)>(loader("glGetQueryObjectui64v"));
        result.available = true;
#define LOAD(member, name)                                                    \
    result.member = reinterpret_cast<decltype(result.member)>(loader(#name)); \
    result.available &= result.member != nullptr;
        GLYPHSHIFT_GL_FUNCTIONS(LOAD)
#undef LOAD
        table = result;
    });
    return table;
}

const GlApi& api() {
    return table;
}
}  // namespace glyphshift::opengl
