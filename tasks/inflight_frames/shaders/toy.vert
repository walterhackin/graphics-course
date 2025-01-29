#version 430
#extension GL_ARB_separate_shader_objects : enable

void main() {
    vec2 xy;
    if (gl_VertexIndex == 0) {
        xy = vec2(-1.0, -1.0);
    } else if (gl_VertexIndex == 1) {
        xy = vec2(3.0, -1.0);
    } else {
        xy = vec2(-1.0, 3.0);
    }
    gl_Position = vec4(xy, 0.0, 1.0);
}
