#pragma once
#include <array>
#include <chrono>

#include "gl_api.hpp"

namespace glyphshift::opengl {
// Opt-in GPU execution time of GlyphShift's own work, enabled with
// GLYPHSHIFT_GPU_TIMING=1. Each measured frame brackets the work with a pair
// of GL_TIMESTAMP queries; results are read a few frames later, only when
// available, so nothing waits for the GPU. Every `report_frames` samples it
// prints one summary line to stderr. Queries belong to the context that is
// current at construction; destroy the timer with that context current.
class GpuTimer {
public:
    static constexpr unsigned report_frames = 600;

    // True when GLYPHSHIFT_GPU_TIMING=1 and the timer query entry points exist.
    static bool requested();

    // `label` names the measured path in the report (static storage).
    explicit GpuTimer(const char* label);
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    // `query_buffer`: the context supports GL_QUERY_BUFFER, which redirects
    // result reads into a bound buffer; the binding is cleared and restored.
    void begin(bool query_buffer = false);
    void end();

private:
    static constexpr unsigned slots = 8;
    const char* label_;
    std::array<GLuint, slots * 2> queries_{};
    std::array<bool, slots> pending_{};
    std::array<float, report_frames> samples_{};
    unsigned slot_ = 0, count_ = 0;
    bool open_ = false;
    std::chrono::steady_clock::time_point since_ = std::chrono::steady_clock::now();

    void collect(unsigned slot);
    void report();
};
}  // namespace glyphshift::opengl
