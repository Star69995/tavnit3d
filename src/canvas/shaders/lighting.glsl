// Shading of solid surfaces, shared by the face and sticker shaders: studio
// lights fixed relative to the camera (so every view is lit the same way) plus
// the document's lights, if any. No shadows.
uniform vec3 cam_normal;
uniform int studio_lighting;
uniform int n_lights;
uniform vec4 light_pos[8];   // xyz; w: 0 lamp, 1 spot, 2 sun
uniform vec4 light_dir[8];   // xyz towards where it shines; w: cosine of the cone's edge
uniform vec4 light_color[8]; // rgb times brightness; w: cosine of where the cone starts fading
uniform float light_dist[8]; // distance to its target, where it's as bright as asked for

// mat: 0 satin, 1 matte, 2 glossy, 3 metal, 4 glass (see document/material.cpp)
vec4 shade_surface(vec3 color, vec3 normal, vec3 pos, int mat)
{
  vec3 v = normalize(cam_normal);
  vec3 n = normalize(normal);
  if(dot(n, v) < 0)
    n = -n; // two-sided
  if(studio_lighting == 0) {
    // original headlight
    float shade = pow(min(1, abs(dot(cam_normal, n))+.1), 1/2.2);
    return vec4(color*shade, mat == 4 ? .35 : 1.);
  }
  vec3 world_up = abs(v.z) > .99 ? vec3(0, 1, 0) : vec3(0, 0, 1);
  vec3 right = normalize(cross(world_up, v));
  vec3 up = cross(v, right);
  vec3 key = normalize(v + .7*up - .45*right);
  vec3 fill = normalize(v - .3*up + .8*right);

  float spec_strength = .15, shininess = 24, diffuse_strength = 1.;
  if(mat == 1) { spec_strength = 0.; }
  else if(mat == 2) { spec_strength = .45; shininess = 140; }
  else if(mat == 3) { spec_strength = .9; shininess = 70; diffuse_strength = .45; }
  else if(mat == 4) { spec_strength = .8; shininess = 160; diffuse_strength = .35; }

  // the studio lights step back a little when the scene has its own
  float studio = n_lights > 0 ? .35 : 1.;
  float diffuse = studio * (.62*max(dot(n, key), 0) + .22*max(dot(n, fill), 0));
  // soft sky/ground ambient
  float hemi = .5 + .5*dot(n, world_up);
  vec3 ambient = mix(vec3(.30, .30, .32), vec3(.42, .43, .46), hemi) * (n_lights > 0 ? .7 : 1.);
  vec3 lit = color * (ambient + diffuse_strength*diffuse);
  float spec = pow(max(dot(n, normalize(key + v)), 0), shininess) * spec_strength * studio;
  vec3 spec_color = mat == 3 ? mix(color, vec3(1), .25) : vec3(1);
  vec3 spec_lit = spec_color * spec;

  for(int i = 0; i < n_lights && i < 8; i++) {
    vec3 l;
    float att = 1.;
    int type = int(light_pos[i].w);
    if(type == 2) {
      l = -normalize(light_dir[i].xyz);
    }
    else {
      vec3 d = light_pos[i].xyz - pos;
      float dist = max(length(d), 1e-3);
      l = d / dist;
      att = min(pow(light_dist[i] / dist, 2), 16.);
      if(type == 1)
        att *= smoothstep(light_dir[i].w, light_color[i].w, dot(-l, normalize(light_dir[i].xyz)));
    }
    float ndl = max(dot(n, l), 0);
    vec3 c = light_color[i].rgb * att;
    lit += color * c * ndl * .5 * diffuse_strength;
    spec_lit += spec_color * c * pow(max(dot(n, normalize(l + v)), 0), shininess) * spec_strength * (ndl > 0 ? 1 : 0);
  }

  if(mat == 3) {
    // metals mostly reflect their surroundings: fake a bright sky / darker floor
    vec3 r = reflect(-v, n);
    float env = smoothstep(-.4, .6, dot(r, world_up));
    lit += color * mix(.15, .75, env) * .6;
  }
  float alpha = 1.;
  if(mat == 4) {
    // glass: see-through face-on, more reflective at grazing angles
    float fresnel = pow(1 - max(dot(n, v), 0), 3);
    vec3 r = reflect(-v, n);
    float env = smoothstep(-.2, .8, dot(r, world_up));
    lit += vec3(.5) * env * (.15 + .6*fresnel);
    alpha = mix(.28, .85, fresnel);
  }
  lit += spec_lit;
  // a soft shoulder instead of clipping, for when several lights add up
  vec3 over = max(lit - .8, 0);
  lit = min(lit, .8) + .2 * (1 - exp(-over / .2));
  return vec4(pow(clamp(lit, 0, 1), vec3(1/1.4)), max(alpha, clamp(max(spec_lit.r, max(spec_lit.g, spec_lit.b)), 0, 1)));
}
