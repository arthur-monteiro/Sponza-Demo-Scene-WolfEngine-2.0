layout (binding = 1) uniform sampler2D tex;

layout(location = 0) in vec2 inTexCoord;

layout(location = 0) out vec4 outColor; 	

void main() 
{
	float color = texture(tex, inTexCoord).a;
	if(color < 0.2)
		discard;

	outColor = vec4(vec3(0.8), color);
}