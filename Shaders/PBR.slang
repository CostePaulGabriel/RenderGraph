// Metallic-roughness BRDF (glTF 2.0 / Filament conventions):
//   specular = GGX distribution * height-correlated Smith visibility * Schlick Fresnel
//   diffuse  = Lambert, scaled by (1 - F) and removed for metals
#pragma once

static const float PI = 3.14159265359;
static const float kMinRoughness = 0.045; // avoids a singular GGX lobe and specular aliasing

struct Surface
{
    float3 baseColor;
    float metallic;
    float roughness; // perceptual
    float occlusion;
    float3 normal;   // world space, normalized
    float transmission; // 0 = opaque; 1 = no diffuse, the rest is refracted (baseColor is then the tint)
    float specular;     // dielectric reflection weight
    float ior;          // dielectric index of refraction (1.5 -> F0 = 0.04)
};

float DielectricF0(float ior)
{
    const float r = (ior - 1.0) / (ior + 1.0);
    return r * r;
}

float3 SpecularColor(Surface surface) { return lerp(DielectricF0(surface.ior).xxx, surface.baseColor, surface.metallic); } // F0
float SpecularWeight(Surface surface) { return lerp(surface.specular, 1.0, surface.metallic); } // metals ignore the dielectric weight
float3 DiffuseColor(Surface surface) { return surface.baseColor * (1.0 - surface.metallic) * (1.0 - surface.transmission); }

float D_GGX(float NoH, float alpha)
{
    const float a2 = alpha * alpha;
    const float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float alpha)
{
    const float a2 = alpha * alpha;
    const float ggxV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    const float ggxL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / max(ggxV + ggxL, 1e-5);
}

float3 F_Schlick(float3 f0, float VoH)
{
    const float f = pow(1.0 - VoH, 5.0);
    return f0 + (1.0 - f0) * f;
}

// Outgoing radiance towards V for a light of radiance 'radiance' arriving from L.
// Light intensities are expressed as the brightness of a white Lambertian surface lit head-on,
// hence the PI factor: (albedo / PI) * PI * intensity = albedo * intensity.
float3 EvaluateLight(Surface surface, float3 V, float3 L, float3 radiance)
{
    const float3 N = surface.normal;
    const float NoL = saturate(dot(N, L));
    if (NoL <= 0.0)
        return 0.0;

    const float3 H = normalize(V + L);
    const float NoV = max(dot(N, V), 1e-4);
    const float NoH = saturate(dot(N, H));
    const float VoH = saturate(dot(V, H));

    const float roughness = max(surface.roughness, kMinRoughness);
    const float alpha = roughness * roughness;

    const float3 F = F_Schlick(SpecularColor(surface), VoH) * SpecularWeight(surface);
    const float3 specular = D_GGX(NoH, alpha) * V_SmithGGXCorrelated(NoV, NoL, alpha) * F;
    const float3 diffuse = (1.0 - F) * DiffuseColor(surface) / PI;

    return (diffuse + specular) * radiance * NoL * PI;
}

// Analytic fit of the split-sum environment BRDF (Karis, "Physically Based Shading on Mobile").
float3 EnvBRDFApprox(float3 f0, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    const float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return f0 * AB.x + AB.y;
}
