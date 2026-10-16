#version 330

layout(location = 0) out vec4 outputColor;
layout(location = 1) out uint pick;
layout(location = 2) out vec4 select;
in vec3 color_to_fragment;
in vec3 normal_to_fragment;
in vec3 pos_to_fragment;
//flat in uint instance_to_fragment;
uniform uint pick_base;
uniform uint flags;
uniform vec3 clipping_value;
uniform ivec3 clipping_op;
uniform int material; // 0 satin, 1 matte, 2 glossy, 3 metal, 4 glass (see document/material.cpp)
flat in float select_alpha_to_frag;

##ubo
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
  if(test_peel(pick_base))
		discard;
  if(should_clip(clipping_op.x, clipping_value.x, pos_to_fragment.x))
    discard;
  if(should_clip(clipping_op.y, clipping_value.y, pos_to_fragment.y))
    discard;
  if(should_clip(clipping_op.z, clipping_value.z, pos_to_fragment.z))
    discard;
  gl_FragDepth =  gl_FragCoord.z *(1+0.0001);
  vec3 color = color_to_fragment;
  if(FLAG_IS_SET(flags, VERTEX_FLAG_HOVER | VERTEX_FLAG_SELECTED))
      color = mix(color, get_color(flags), .5);
  int mat = material & 0xff;
  bool highlighted = (material & 0x100) != 0; // the picked object
  if(highlighted)
      color = mix(color, vec3(.18, .42, .94), .12); // barely: its own colour stays recognisable
  outputColor = shade_surface(color, normal_to_fragment, pos_to_fragment, mat);
  if(highlighted) {
      // a blue glow towards its outline instead
      float facing = abs(dot(normalize(normal_to_fragment), normalize(cam_normal)));
      float rim = pow(1 - facing, 2.5);
      outputColor.rgb = mix(outputColor.rgb, vec3(.25, .52, 1), rim * .55);
  }
  select = outputColor*select_alpha_to_frag;
  pick = pick_base;
}
