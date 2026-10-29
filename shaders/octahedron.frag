#version 450

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformData {
    vec4 cam_pos;
    vec4 light_dir;
    vec4 light_color;
    vec4 ambient_color;
    vec4 albedo;
    vec4 specular_color;
    vec4 misc;
} u;

void main() {
    vec3 N = normalize(fragNormal);
    vec3 L = normalize(-u.light_dir.xyz);
    vec3 V = normalize(u.cam_pos.xyz - fragPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), u.misc.x);

    vec3 ambient = u.ambient_color.rgb * u.misc.y;
    vec3 diffuse = u.light_color.rgb * u.light_dir.w * diff * u.albedo.rgb;
    vec3 specular = u.specular_color.rgb * u.light_color.rgb * u.light_dir.w * spec;

    outColor = vec4(ambient + diffuse + specular, 1.0);
}