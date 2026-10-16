#version 330

layout(location = 0) out vec4 outputColor;
layout(location = 1) out uint pick;
layout(location = 2) out vec4 select;
in vec3 normal_to_fragment;
in vec3 pos_to_fragment;
in vec2 uv_to_fragment;
uniform sampler2D tex;
uniform vec3 clipping_value;
uniform ivec3 clipping_op;

##lighting

bool should_clip(int op, float clip_value, float value)
{
  if(op == 1)
    return value > clip_value;
  else if(op == -1)
    return value < clip_value;
  else
    return false;
}

void main() {
  if(should_clip(clipping_op.x, clipping_value.x, pos_to_fragment.x))
    discard;
  if(should_clip(clipping_op.y, clipping_value.y, pos_to_fragment.y))
    discard;
  if(should_clip(clipping_op.z, clipping_value.z, pos_to_fragment.z))
    discard;
  vec4 c = texture(tex, uv_to_fragment);
  if(c.a < .01)
    discard;
  // just in front of the surface it lies on (faces push themselves back by 1e-4)
  gl_FragDepth = gl_FragCoord.z * (1 + 0.00005);

  // lit like a satin surface
  vec3 lit = shade_surface(c.rgb, normal_to_fragment, pos_to_fragment, 0).rgb;
  outputColor = vec4(lit, c.a);
  select = vec4(0);
  pick = 0u;
}
