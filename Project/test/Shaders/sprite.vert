#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 v_TexCoord;

uniform mat4 model;
uniform mat4 projection;

uniform vec2 uvOffset;
uniform vec2 uvScale;

void main()
{
    gl_Position = projection * model * vec4(aPos, 1.0);
    v_TexCoord = aTexCoord * uvScale + uvOffset;
}