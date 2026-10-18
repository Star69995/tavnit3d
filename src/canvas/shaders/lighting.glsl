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

float wood_hash(vec3 p)
{
  p = fract(p * .3183099 + .1);
  p *= 17.;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float wood_noise(vec3 x)
{
  vec3 i = floor(x), f = fract(x);
  f = f*f*(3. - 2.*f);
  return mix(mix(mix(wood_hash(i), wood_hash(i + vec3(1,0,0)), f.x),
                 mix(wood_hash(i + vec3(0,1,0)), wood_hash(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(wood_hash(i + vec3(0,0,1)), wood_hash(i + vec3(1,0,1)), f.x),
                 mix(wood_hash(i + vec3(0,1,1)), wood_hash(i + vec3(1,1,1)), f.x), f.y), f.z);
}

// Grain of a plank along X (pos in mm): growth rings around an axis parallel to X, so the top
// shows stripes and the ends show rings. The body's colour stains it (grey = natural wood).
vec3 wood_color(vec3 color, vec3 pos)
{
  vec3 p = pos / 10.;
  // a slight waviness along the plank, finer fibres across the grain
  float wobble = wood_noise(vec3(p.x*.15, p.y*.6, p.z*.6)) * 1.6;
  float r = length(vec2(p.y, p.z) + vec2(.8, 1.3)*wobble) * 1.8 + wood_noise(vec3(p.x*.08, p.y*.3, p.z*.3)) * 1.2;
  float ring = smoothstep(.15, .85, fract(r));
  ring = ring*ring;
  float fibre = wood_noise(vec3(p.x*.7, p.y*18., p.z*18.));
  vec3 light = vec3(.80, .58, .36), dark = vec3(.52, .33, .18);
  vec3 wood = mix(light, dark, ring * .85) * (.9 + .2*fibre);
  float luma = dot(color, vec3(.299, .587, .114));
  vec3 tint = clamp(color / max(luma, .05), 0., 1.6);
  return clamp(wood * tint, 0., 1.);
}

// mat: 0 satin, 1 matte, 2 glossy, 3 metal, 4 glass, 5 wood (see document/material.cpp)
vec4 shade_surface(vec3 color, vec3 normal, vec3 pos, int mat)
{
  if(mat == 5)
    color = wood_color(color, pos);
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
  else if(mat == 5) { spec_strength = .1; shininess = 18; }
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
