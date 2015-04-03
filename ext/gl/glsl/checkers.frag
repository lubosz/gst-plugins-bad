#version 330

in vec2 out_uv;
out vec4 frag_color;
uniform float checker_width;

void main() {
  vec2 xy_index = floor(out_uv.xy / checker_width);
  vec2 xy_mod = mod(xy_index, vec2(2.0, 2.0));
  float result = step(mod(xy_mod.x + xy_mod.y, 2.0), 0.5);
  frag_color = vec4(result, 1.0 - result, 0.0, 1.0);
}
