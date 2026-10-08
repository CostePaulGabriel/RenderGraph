#pragma once

// PerspectiveCamera: position + yaw/pitch orientation + perspective projection.
// Left-handed, +Y up, looks along +Z when yaw = pitch = 0, depth range [0, 1] (see Math.h).
// Matrices are recomputed when a setter changes them, so the getters are cheap and const.

#include <cstdint>

#include "Scene/Math.h"

namespace LynxEngine
{
    class PerspectiveCamera
    {
    public:
        PerspectiveCamera();
        PerspectiveCamera(float fovY, float aspectRatio, float nearZ, float farZ);

        // ---- Projection ---------------------------------------------------------------

        void SetPerspective(float fovY, float aspectRatio, float nearZ, float farZ);
        void SetFovY(float radians);
        void SetAspectRatio(float aspectRatio);
        void SetAspectRatio(uint32_t width, uint32_t height);
        void SetClipPlanes(float nearZ, float farZ);

        float GetFovY() const { return m_FovY; }
        float GetAspectRatio() const { return m_AspectRatio; }
        float GetNearZ() const { return m_NearZ; }
        float GetFarZ() const { return m_FarZ; }

        // ---- View ---------------------------------------------------------------------

        void SetPosition(const float3& position);
        void LookAt(const float3& target);                          // keeps the position
        void LookAt(const float3& position, const float3& target);
        void SetRotation(float yaw, float pitch);                   // radians; pitch is clamped to avoid the poles
        void Rotate(float yawDelta, float pitchDelta);
        void Move(const float3& localDelta);                        // x = right, y = up, z = forward

        const float3& GetPosition() const { return m_Position; }
        float GetYaw() const { return m_Yaw; }
        float GetPitch() const { return m_Pitch; }
        float3 GetForward() const;
        float3 GetRight() const;
        float3 GetUp() const;

        // ---- Matrices -----------------------------------------------------------------

        const float4x4& GetView() const { return m_View; }
        const float4x4& GetProjection() const { return m_Projection; }
        const float4x4& GetViewProjection() const { return m_ViewProjection; }
        const float4x4& GetInverseViewProjection() const { return m_InverseViewProjection; }

    private:
        void UpdateView();
        void UpdateProjection();
        void UpdateViewProjection();

        float3 m_Position{ 0.0f, 3.0f, -8.0f };
        float m_Yaw = 0.0f;
        float m_Pitch = 0.0f;

        float m_FovY = 1.0f;
        float m_AspectRatio = 16.0f / 9.0f;
        float m_NearZ = 0.1f;
        float m_FarZ = 100.0f;

        float4x4 m_View;
        float4x4 m_Projection;
        float4x4 m_ViewProjection;
        float4x4 m_InverseViewProjection;
    };
}
