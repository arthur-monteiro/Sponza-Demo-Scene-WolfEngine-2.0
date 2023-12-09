layout (binding = 0) uniform sampler2D tex;

layout (location = 0) in vec2 inTexCoords;

layout (location = 0) out vec4 outColor;
layout (location = 1) out vec4 outVelocity;

void main() 
{
	outColor = vec4(texture(tex, inTexCoords).rgba);
	outVelocity = vec4(0.0, 0.0, 0.0, outColor.a > 0.0 ? 1.0 : 0.0);
}
