#version 330

in vec2 out_uv;
out vec4 frag_color;

uniform float time;

float rand(vec2 co) {
  return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
  frag_color = rand(time * out_uv) * vec4(1);
}
