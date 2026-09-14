#version 460

out vec2 f_pos;

vec2 verts[] = vec2[](
	vec2(0,0), vec2(1,0), vec2(0,1),
	vec2(1,1), vec2(0,1), vec2(1,0)
);

void main() {
	vec2 pos = verts[gl_VertexID];
	gl_Position = vec4(
		(2*pos.x)-1,
		-(2*pos.y)+1,
		0, 1
	);
	f_pos = pos;
}
