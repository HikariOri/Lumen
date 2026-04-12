#version 450

layout(location = 0) out vec4 out_color;

layout (set = 0, binding = 0) uniform FrameUBO {
    mat4 view;
    mat4 proj;
    mat4 viewProj;

    vec3 cameraPos;
    float time;

    vec2 sceenSize;

    vec4 exposureIblMips;

    int debugMode;
    
} frameUBO;

layout (set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    mat4 normalMatrix;
} objectUBO;

vec3 palette( float t ) {
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(0.263,0.416,0.557);

    return a + b*cos( 6.28318*(c*t+d) );
}

//https://www.shadertoy.com/view/mtyGWy
void mainImage( out vec4 fragColor, in vec2 fragCoord ) {
    vec2 uv = (fragCoord * 2.0 - frameUBO.sceenSize.xy) / frameUBO.sceenSize.y;
    vec2 uv0 = uv;
    vec3 finalColor = vec3(0.0);
    
    for (float i = 0.0; i < 4.0; i++) {
        uv = fract(uv * 1.5) - 0.5;

        float d = length(uv) * exp(-length(uv0));

        vec3 col = palette(length(uv0) + i*.4 + frameUBO.time*.4);

        d = sin(d*8. + frameUBO.time)/8.;
        d = abs(d);

        d = pow(0.01 / d, 1.2);

        finalColor += col * d;
    }
        
    fragColor = vec4(finalColor, 1.0);
}

void main() {
    // vec2 uv = gl_FragCoord.xy / frameUBO.sceenSize;
    // out_color = vec4(0.5 + 0.5 * cos(frameUBO.time + uv.xyx + vec3(0, 2, 4)), 1.0);
    mainImage(out_color, gl_FragCoord.xy);
}
