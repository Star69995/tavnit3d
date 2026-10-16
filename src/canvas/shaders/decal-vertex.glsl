#version 330

in vec3 position;
in vec3 normal;
in vec2 uv;

out vec3 normal_to_fragment;
out vec3 pos_to_fragment;
out vec2 uv_to_fragment;

uniform mat4 view;
uniform mat4 proj;

void main()
{
    gl_Position = (proj * view) * vec4(position, 1);
    pos_to_fragment = position;
    normal_to_fragment = normal;
    uv_to_fragment = uv;
}
