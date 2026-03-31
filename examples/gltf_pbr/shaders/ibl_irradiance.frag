#version 450

layout(location = 0) in vec3 vLocalDir;

layout(set = 0, binding = 0) uniform samplerCube envMap;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265358979323846;

void main() {
    vec3 N = normalize(vLocalDir);
    vec3 irradiance = vec3(0.0);
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);
    float sampleDelta = 0.085;
    float count = 0.0;
    for (float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            vec3 ts = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi),
                           cos(theta));
            vec3 sampleVec = tangent * ts.x + bitangent * ts.y + N * ts.z;
            irradiance += textureLod(envMap, sampleVec, 0.0).rgb * cos(theta)
                         * sin(theta);
            count += 1.0;
        }
    }
    irradiance = PI * irradiance * (1.0 / max(count, 1.0));
    outColor = vec4(irradiance, 1.0);
}
