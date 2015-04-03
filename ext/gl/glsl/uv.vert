#version 330
in vec4 position;
in vec2 uv;
out vec2 out_uv;

void main() {
  gl_Position = position;
  out_uv = uv;
}
