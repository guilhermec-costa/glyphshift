#version 330 core
#ifdef VULKAN
#extension GL_ARB_separate_shader_objects : enable
#define VERTEX_INDEX gl_VertexIndex
#else
#define VERTEX_INDEX gl_VertexID
#endif
void main() {
    vec2 p = vec2(float((VERTEX_INDEX << 1) & 2), float(VERTEX_INDEX & 2));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
