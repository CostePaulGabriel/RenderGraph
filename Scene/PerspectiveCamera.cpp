#include "Scene/PerspectiveCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace LynxEngine
{
    namespace
    {
        constexpr float kMaxPitch = std::numbers::pi_v<float> * 0.5f - 0.001f;
        constexpr float3 kWorldUp{ 0.0f, 1.0f, 0.0f };
    }

    PerspectiveCamera::PerspectiveCamera()
    {
        UpdateView();
        UpdateProjection();
    }

    PerspectiveCamera::PerspectiveCamera(float fovY, float aspectRatio, float nearZ, float farZ)
        : m_FovY(fovY), m_AspectRatio(aspectRatio), m_NearZ(nearZ), m_FarZ(farZ)
    {
        UpdateView();
        UpdateProjection();
    }

    // ---- Projection -----------------------------------------------------------------------

    void PerspectiveCamera::SetPerspective(float fovY, float aspectRatio, float nearZ, float farZ)
    {
        m_FovY = fovY;
        m_AspectRatio = aspectRatio;
        m_NearZ = nearZ;
        m_FarZ = farZ;
        UpdateProjection();
    }

    void PerspectiveCamera::SetFovY(float radians)
    {
        m_FovY = radians;
        UpdateProjection();
    }

    void PerspectiveCamera::SetAspectRatio(float aspectRatio)
    {
        m_AspectRatio = aspectRatio;
        UpdateProjection();
    }

    void PerspectiveCamera::SetAspectRatio(uint32_t width, uint32_t height)
    {
        SetAspectRatio(float(width) / float(std::max(1u, height)));
    }

    void PerspectiveCamera::SetClipPlanes(float nearZ, float farZ)
    {
        m_NearZ = nearZ;
        m_FarZ = farZ;
        UpdateProjection();
    }

    // ---- View -----------------------------------------------------------------------------

    void PerspectiveCamera::SetPosition(const float3& position)
    {
        m_Position = position;
        UpdateView();
    }

    void PerspectiveCamera::LookAt(const float3& target)
    {
        const float3 direction = Normalize(target - m_Position);
        m_Yaw = std::atan2(direction.x, direction.z);
        m_Pitch = std::clamp(std::asin(std::clamp(direction.y, -1.0f, 1.0f)), -kMaxPitch, kMaxPitch);
        UpdateView();
    }

    void PerspectiveCamera::LookAt(const float3& position, const float3& target)
    {
        m_Position = position;
        LookAt(target);
    }

    void PerspectiveCamera::SetRotation(float yaw, float pitch)
    {
        m_Yaw = yaw;
        m_Pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
        UpdateView();
    }

    void PerspectiveCamera::Rotate(float yawDelta, float pitchDelta)
    {
        SetRotation(m_Yaw + yawDelta, m_Pitch + pitchDelta);
    }

    void PerspectiveCamera::Move(const float3& localDelta)
    {
        m_Position = m_Position + GetRight() * localDelta.x + GetUp() * localDelta.y + GetForward() * localDelta.z;
        UpdateView();
    }

    float3 PerspectiveCamera::GetForward() const
    {
        const float cosPitch = std::cos(m_Pitch);
        return { std::sin(m_Yaw) * cosPitch, std::sin(m_Pitch), std::cos(m_Yaw) * cosPitch };
    }

    float3 PerspectiveCamera::GetRight() const
    {
        return Normalize(Cross(kWorldUp, GetForward()));
    }

    float3 PerspectiveCamera::GetUp() const
    {
        return Cross(GetForward(), GetRight());
    }

    // ---- Matrices -------------------------------------------------------------------------

    void PerspectiveCamera::UpdateView()
    {
        m_View = float4x4::LookAtLH(m_Position, m_Position + GetForward(), kWorldUp);
        UpdateViewProjection();
    }

    void PerspectiveCamera::UpdateProjection()
    {
        m_Projection = float4x4::PerspectiveFovLH(m_FovY, m_AspectRatio, m_NearZ, m_FarZ);
        UpdateViewProjection();
    }

    void PerspectiveCamera::UpdateViewProjection()
    {
        m_ViewProjection = m_Projection * m_View;
        m_InverseViewProjection = m_ViewProjection.Inverse();
    }
}
