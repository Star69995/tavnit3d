#version 330

layout(location = 0) out vec4 outputColor;
layout(location = 1) out uint pick;
layout(location = 2) out vec4 select;
in vec3 color_to_fragment;
in vec3 normal_to_fragment;
in vec3 pos_to_fragment;
//flat in uint instance_to_fragment;
uniform vec3 cam_normal;
uniform uint pick_base;
uniform uint flags;
uniform vec3 clipping_value;
uniform ivec3 clipping_op;
uniform int material; // 0 satin, 1 matte, 2 glossy, 3 metal
uniform int studio_lighting;
flat in float select_alpha_to_frag;

##ubo

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
  if(studio_lighting == 0) {
    // original headlight
    float shade = pow(min(1, abs(dot(cam_normal, normal_to_fragment))+.1), 1/2.2);
    outputColor = vec4(color*(shade), 1);
  }
  else {
    // lights fixed relative to the camera, so every view is lit the same way
    vec3 v = normalize(cam_normal);
    vec3 n = normalize(normal_to_fragment);
    if(dot(n, v) < 0)
      n = -n; // two-sided
    vec3 world_up = abs(v.z) > .99 ? vec3(0, 1, 0) : vec3(0, 0, 1);
    vec3 right = normalize(cross(world_up, v));
    vec3 up = cross(v, right);
    vec3 key = normalize(v + .7*up - .45*right);
    vec3 fill = normalize(v - .3*up + .8*right);

    float spec_strength = .15, shininess = 24, diffuse_strength = 1.;
    if(material == 1) { spec_strength = 0.; }
    else if(material == 2) { spec_strength = .45; shininess = 140; }
    else if(material == 3) { spec_strength = .9; shininess = 70; diffuse_strength = .45; }

    float diffuse = .62*max(dot(n, key), 0) + .22*max(dot(n, fill), 0);
    // soft sky/ground ambient
    float hemi = .5 + .5*dot(n, world_up);
    vec3 ambient = mix(vec3(.30, .30, .32), vec3(.42, .43, .46), hemi);
    vec3 lit = color * (ambient + diffuse_strength*diffuse);
    float spec = pow(max(dot(n, normalize(key + v)), 0), shininess) * spec_strength;
    vec3 spec_color = material == 3 ? mix(color, vec3(1), .25) : vec3(1);
    if(material == 3) {
      // metals mostly reflect their surroundings: fake a bright sky / darker floor
      vec3 r = reflect(-v, n);
      float env = smoothstep(-.4, .6, dot(r, world_up));
      lit += color * mix(.15, .75, env) * .6;
    }
    lit += spec_color * spec;
    outputColor = vec4(pow(clamp(lit, 0, 1), vec3(1/1.4)), 1);
  }
  select = outputColor*select_alpha_to_frag;
  pick = pick_base;
}
