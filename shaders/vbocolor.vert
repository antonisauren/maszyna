layout(location = 0) in vec3 v_vert;
layout(location = 1) in vec3 v_color;

out vec3 f_color;

#include <common>

void main()
{
	gl_Position = projection * modelview * vec4(v_vert, 1.0f);
	f_color = v_color;
}