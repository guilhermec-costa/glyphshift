#include "gpu_timer.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace glyphshift::opengl {
bool GpuTimer::requested() {
    static const bool value = [] {
        const char* enabled = std::getenv("GLYPHSHIFT_GPU_TIMING");
        const auto& gl = api();
        return enabled && std::strcmp(enabled, "1") == 0 && gl.GenQueries && gl.DeleteQueries &&
               gl.QueryCounter && gl.GetQueryObjectiv && gl.GetQueryObjectui64v;
    }();
    return value;
}

GpuTimer::GpuTimer(const char* label) : label_(label) {
    api().GenQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

GpuTimer::~GpuTimer() {
    api().DeleteQueries(static_cast<GLsizei>(queries_.size()), queries_.data());
}

void GpuTimer::begin(bool query_buffer) {
    const auto& gl = api();
    slot_ = (slot_ + 1) % slots;
    open_ = false;
    if (pending_[slot_]) {
        GLint buffer = 0;
        if (query_buffer) {
            gl.GetIntegerv(GL_QUERY_BUFFER_BINDING, &buffer);
            if (buffer)
                gl.BindBuffer(GL_QUERY_BUFFER, 0);
        }
        collect(slot_);
        if (buffer)
            gl.BindBuffer(GL_QUERY_BUFFER, static_cast<GLuint>(buffer));
        if (pending_[slot_])
            return;  // The GPU is more than `slots` frames behind: skip, never wait.
    }
    gl.QueryCounter(queries_[slot_ * 2], GL_TIMESTAMP);
    open_ = true;
}

void GpuTimer::end() {
    if (!open_)
        return;
    api().QueryCounter(queries_[slot_ * 2 + 1], GL_TIMESTAMP);
    pending_[slot_] = true;
    open_ = false;
}

void GpuTimer::collect(unsigned slot) {
    const auto& gl = api();
    GLint available = 0;
    gl.GetQueryObjectiv(queries_[slot * 2 + 1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available)
        return;
    GLuint64 start = 0, stop = 0;
    gl.GetQueryObjectui64v(queries_[slot * 2], GL_QUERY_RESULT, &start);
    gl.GetQueryObjectui64v(queries_[slot * 2 + 1], GL_QUERY_RESULT, &stop);
    pending_[slot] = false;
    samples_[count_++] = stop > start ? static_cast<float>(stop - start) * 1e-6F : 0.0F;
    if (count_ == samples_.size())
        report();
}

void GpuTimer::report() {
    auto& values = samples_;
    double sum = 0;
    for (const auto value : values)
        sum += value;
    std::sort(values.begin(), values.end());
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - since_).count();
    since_ = now;
    char line[192];
    const int size = std::snprintf(line,
        sizeof(line),
        "glyphshift: GPU %s: %u frames (%.0f/s), mean %.3f ms, median %.3f ms, p95 %.3f ms, "
        "max %.3f ms\n",
        label_,
        count_,
        count_ / seconds,
        sum / count_,
        static_cast<double>(values[count_ / 2]),
        static_cast<double>(values[count_ * 95 / 100]),
        static_cast<double>(values[count_ - 1]));
    if (size > 0) {
        const auto ignored =
            write(STDERR_FILENO, line, std::min(static_cast<std::size_t>(size), sizeof(line) - 1));
        (void)ignored;
    }
    count_ = 0;
}
}  // namespace glyphshift::opengl
