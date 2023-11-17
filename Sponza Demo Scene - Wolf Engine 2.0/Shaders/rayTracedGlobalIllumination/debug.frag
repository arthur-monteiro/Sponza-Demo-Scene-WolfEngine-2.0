#extension GL_ARB_separate_shader_objects : enable
#extension GL_EXT_nonuniform_qualifier : enable

layout (early_fragment_tests) in;

layout (location = 0) out vec4 outColor;

void main() 
{
    outColor = vec4(1.0, 1.0, 1.0, 1.0);
}