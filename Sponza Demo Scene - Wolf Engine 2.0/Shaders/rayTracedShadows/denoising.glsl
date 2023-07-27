const uint KERNEL_RADIUS = 5;

float computeShadows(vec3 normal)
{
    float refPixelDepth = imageLoad(depthTexture, ivec2(gl_FragCoord.xy)).r;

    uint nSamples = 0;
    float sumShadows = 0.0f;
    for(int x = int(gl_FragCoord.x - KERNEL_RADIUS); x < gl_FragCoord.x + KERNEL_RADIUS; ++x)
    {
        for(int y = int(gl_FragCoord.y - KERNEL_RADIUS); y < gl_FragCoord.y + KERNEL_RADIUS; ++y)
        {
            float depth = imageLoad(depthTexture, ivec2(x, y)).r;

            if(abs(depth - refPixelDepth) < 0.001)
            {
                sumShadows += imageLoad(shadowMask, ivec2(x, y)).r;
                nSamples++;
            }
        }
    }

    return sumShadows / float(nSamples);
}