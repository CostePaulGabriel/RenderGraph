#pragma once

// Tiny math helpers for the sample. Matrices are row-major in memory (m[row][col]) and use
// column vectors (p' = M * p), left-handed, depth range [0, 1]. Shaders are compiled with
// row-major matrix layout and use mul(M, v), so matrices are uploaded as they are.

#include <cmath>

namespace LynxEngine
{
    struct float2
    {
        float x = 0, y = 0;
    };

    struct float3
    {
        float x = 0, y = 0, z = 0;

        float3 operator+(const float3& o) const { return { x + o.x, y + o.y, z + o.z }; }
        float3 operator-(const float3& o) const { return { x - o.x, y - o.y, z - o.z }; }
        float3 operator*(float s) const { return { x * s, y * s, z * s }; }
    };

    inline float Dot(const float3& a, const float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    inline float3 Cross(const float3& a, const float3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    inline float3 Normalize(const float3& v)
    {
        const float length = std::sqrt(Dot(v, v));
        return length > 0.0f ? v * (1.0f / length) : v;
    }

    struct float4x4
    {
        float m[4][4] = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };

        static float4x4 Identity() { return {}; }

        static float4x4 Translation(const float3& t)
        {
            float4x4 r;
            r.m[0][3] = t.x;
            r.m[1][3] = t.y;
            r.m[2][3] = t.z;
            return r;
        }

        static float4x4 Scale(const float3& s)
        {
            float4x4 r;
            r.m[0][0] = s.x;
            r.m[1][1] = s.y;
            r.m[2][2] = s.z;
            return r;
        }

        static float4x4 RotationY(float radians)
        {
            const float c = std::cos(radians), s = std::sin(radians);
            float4x4 r;
            r.m[0][0] = c;  r.m[0][2] = s;
            r.m[2][0] = -s; r.m[2][2] = c;
            return r;
        }

        static float4x4 RotationX(float radians)
        {
            const float c = std::cos(radians), s = std::sin(radians);
            float4x4 r;
            r.m[1][1] = c; r.m[1][2] = -s;
            r.m[2][1] = s; r.m[2][2] = c;
            return r;
        }

        static float4x4 LookAtLH(const float3& eye, const float3& target, const float3& up)
        {
            const float3 z = Normalize(target - eye);
            const float3 x = Normalize(Cross(up, z));
            const float3 y = Cross(z, x);
            float4x4 r;
            r.m[0][0] = x.x; r.m[0][1] = x.y; r.m[0][2] = x.z; r.m[0][3] = -Dot(x, eye);
            r.m[1][0] = y.x; r.m[1][1] = y.y; r.m[1][2] = y.z; r.m[1][3] = -Dot(y, eye);
            r.m[2][0] = z.x; r.m[2][1] = z.y; r.m[2][2] = z.z; r.m[2][3] = -Dot(z, eye);
            return r;
        }

        static float4x4 PerspectiveFovLH(float fovY, float aspect, float nearZ, float farZ)
        {
            const float f = 1.0f / std::tan(fovY * 0.5f);
            float4x4 r;
            r.m[0][0] = f / aspect;
            r.m[1][1] = f;
            r.m[2][2] = farZ / (farZ - nearZ);
            r.m[2][3] = -nearZ * farZ / (farZ - nearZ);
            r.m[3][2] = 1.0f;
            r.m[3][3] = 0.0f;
            return r;
        }

        float4x4 operator*(const float4x4& o) const
        {
            float4x4 r;
            for (int i = 0; i < 4; ++i)
            {
                for (int j = 0; j < 4; ++j)
                {
                    r.m[i][j] = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        r.m[i][j] += m[i][k] * o.m[k][j];
                }
            }
            return r;
        }

        // General inverse (cofactors). Returns identity for singular matrices.
        float4x4 Inverse() const
        {
            const float* a = &m[0][0];
            float inv[16];
            inv[0] = a[5] * a[10] * a[15] - a[5] * a[11] * a[14] - a[9] * a[6] * a[15] + a[9] * a[7] * a[14] + a[13] * a[6] * a[11] - a[13] * a[7] * a[10];
            inv[4] = -a[4] * a[10] * a[15] + a[4] * a[11] * a[14] + a[8] * a[6] * a[15] - a[8] * a[7] * a[14] - a[12] * a[6] * a[11] + a[12] * a[7] * a[10];
            inv[8] = a[4] * a[9] * a[15] - a[4] * a[11] * a[13] - a[8] * a[5] * a[15] + a[8] * a[7] * a[13] + a[12] * a[5] * a[11] - a[12] * a[7] * a[9];
            inv[12] = -a[4] * a[9] * a[14] + a[4] * a[10] * a[13] + a[8] * a[5] * a[14] - a[8] * a[6] * a[13] - a[12] * a[5] * a[10] + a[12] * a[6] * a[9];
            inv[1] = -a[1] * a[10] * a[15] + a[1] * a[11] * a[14] + a[9] * a[2] * a[15] - a[9] * a[3] * a[14] - a[13] * a[2] * a[11] + a[13] * a[3] * a[10];
            inv[5] = a[0] * a[10] * a[15] - a[0] * a[11] * a[14] - a[8] * a[2] * a[15] + a[8] * a[3] * a[14] + a[12] * a[2] * a[11] - a[12] * a[3] * a[10];
            inv[9] = -a[0] * a[9] * a[15] + a[0] * a[11] * a[13] + a[8] * a[1] * a[15] - a[8] * a[3] * a[13] - a[12] * a[1] * a[11] + a[12] * a[3] * a[9];
            inv[13] = a[0] * a[9] * a[14] - a[0] * a[10] * a[13] - a[8] * a[1] * a[14] + a[8] * a[2] * a[13] + a[12] * a[1] * a[10] - a[12] * a[2] * a[9];
            inv[2] = a[1] * a[6] * a[15] - a[1] * a[7] * a[14] - a[5] * a[2] * a[15] + a[5] * a[3] * a[14] + a[13] * a[2] * a[7] - a[13] * a[3] * a[6];
            inv[6] = -a[0] * a[6] * a[15] + a[0] * a[7] * a[14] + a[4] * a[2] * a[15] - a[4] * a[3] * a[14] - a[12] * a[2] * a[7] + a[12] * a[3] * a[6];
            inv[10] = a[0] * a[5] * a[15] - a[0] * a[7] * a[13] - a[4] * a[1] * a[15] + a[4] * a[3] * a[13] + a[12] * a[1] * a[7] - a[12] * a[3] * a[5];
            inv[14] = -a[0] * a[5] * a[14] + a[0] * a[6] * a[13] + a[4] * a[1] * a[14] - a[4] * a[2] * a[13] - a[12] * a[1] * a[6] + a[12] * a[2] * a[5];
            inv[3] = -a[1] * a[6] * a[11] + a[1] * a[7] * a[10] + a[5] * a[2] * a[11] - a[5] * a[3] * a[10] - a[9] * a[2] * a[7] + a[9] * a[3] * a[6];
            inv[7] = a[0] * a[6] * a[11] - a[0] * a[7] * a[10] - a[4] * a[2] * a[11] + a[4] * a[3] * a[10] + a[8] * a[2] * a[7] - a[8] * a[3] * a[6];
            inv[11] = -a[0] * a[5] * a[11] + a[0] * a[7] * a[9] + a[4] * a[1] * a[11] - a[4] * a[3] * a[9] - a[8] * a[1] * a[7] + a[8] * a[3] * a[5];
            inv[15] = a[0] * a[5] * a[10] - a[0] * a[6] * a[9] - a[4] * a[1] * a[10] + a[4] * a[2] * a[9] + a[8] * a[1] * a[6] - a[8] * a[2] * a[5];

            const float det = a[0] * inv[0] + a[1] * inv[4] + a[2] * inv[8] + a[3] * inv[12];
            if (std::abs(det) < 1e-12f)
                return {};

            float4x4 r;
            for (int i = 0; i < 16; ++i)
                (&r.m[0][0])[i] = inv[i] / det;
            return r;
        }
    };
}
