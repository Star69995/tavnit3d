#version 330

layout(location = 0) out vec4 outputColor;
layout(location = 1) out uint pick;
layout(location = 2) out vec4 select;
in vec3 normal_to_fragment;
in vec3 pos_to_fragment;
in vec2 uv_to_fragment;
uniform sampler2D tex;
uniform vec3 cam_normal;
uniform int studio_lighting;
uniform vec3 clipping_value;
uniform ivec3 clipping_op;

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

  // lit like a satin surface (see face-fragment.glsl)
  vec3 v = normalize(cam_normal);
  vec3 n = normalize(normal_to_fragment);
  if(dot(n, v) < 0)
    n = -n;
  vec3 lit;
  if(studio_lighting == 0) {
    float shade = pow(min(1, abs(dot(v, n))+.1), 1/2.2);
    lit = c.rgb * shade;
  }
  else {
    vec3 world_up = abs(v.z) > .99 ? vec3(0, 1, 0) : vec3(0, 0, 1);
    vec3 right = normalize(cross(world_up, v));
    vec3 up = cross(v, right);
    vec3 key = normalize(v + .7*up - .45*right);
    vec3 fill = normalize(v - .3*up + .8*right);
    float diffuse = .62*max(dot(n, key), 0) + .22*max(dot(n, fill), 0);
    float hemi = .5 + .5*dot(n, world_up);
    vec3 ambient = mix(vec3(.30, .30, .32), vec3(.42, .43, .46), hemi);
    lit = c.rgb * (ambient + diffuse);
    lit += vec3(1) * pow(max(dot(n, normalize(key + v)), 0), 24) * .15;
    lit = pow(clamp(lit, 0, 1), vec3(1/1.4));
  }
  outputColor = vec4(lit, c.a);
  select = vec4(0);
  pick = 0u;
}
