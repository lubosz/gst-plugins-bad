#version 330

in vec4 position;
in vec2 uv;
out vec2 fractal_position;

uniform float aspect_ratio;

void main() {
  gl_Position = position;
  fractal_position = vec2(uv.y - 0.8, aspect_ratio * (uv.x - 0.5));
  fractal_position *= 2.5;
}
