#include "TrueHUDIntegration.h"

#include "TrueHUDAPI.h"
#include "utils/Logger.h"

namespace truehud
{
	namespace
	{
		std::atomic<TRUEHUD_API::IVTrueHUD3*> api{ nullptr };
		std::atomic_bool motionWarpVisible{ false };
		std::atomic_bool ledgeProtectionVisible{ false };

		constexpr float kMotionDuration = 1.0F;
		constexpr float kGroundProbeDuration = 0.15F;
		constexpr float kThickness = 3.0F;
		constexpr float kActualHeightOffset = 8.0F;
		constexpr float kVectorEpsilon = 1.0e-5F;
		constexpr std::uint32_t kYellow = 0xFFFF00FF;
		constexpr std::uint32_t kGreen = 0x00FF00FF;
		constexpr std::uint32_t kRed = 0xFF0000FF;
		constexpr std::uint32_t kCyan = 0x00FFFFFF;

		bool HasLength(const RE::NiPoint3& a_vector)
		{
			return std::hypot(a_vector.x, a_vector.y) > kVectorEpsilon ||
				   std::abs(a_vector.z) > kVectorEpsilon;
		}

		RE::NiPoint3 LocalToWorld(const RE::NiPoint3& a_local, float a_yaw)
		{
			const float sine = std::sin(a_yaw);
			const float cosine = std::cos(a_yaw);
			return {
				a_local.x * cosine + a_local.y * sine,
				-a_local.x * sine + a_local.y * cosine,
				a_local.z
			};
		}
	}

	void Initialize()
	{
		auto* acquired = static_cast<TRUEHUD_API::IVTrueHUD3*>(
			TRUEHUD_API::RequestPluginAPI(TRUEHUD_API::InterfaceVersion::V3));
		api.store(acquired, std::memory_order_release);
		if (acquired) {
			logger::info("[AMR-DIAG][TrueHUD] API V3 acquired; Page Up toggles motion-warp debug, Page Down toggles ledge debug");
		} else {
			logger::warn("[AMR-DIAG][TrueHUD] API V3 unavailable; debug visualization disabled");
		}
	}

	bool ToggleMotionWarpVisibility()
	{
		const bool visible = !motionWarpVisible.load(std::memory_order_relaxed);
		motionWarpVisible.store(visible, std::memory_order_relaxed);
		return visible;
	}

	bool ToggleLedgeProtectionVisibility()
	{
		const bool visible = !ledgeProtectionVisible.load(std::memory_order_relaxed);
		ledgeProtectionVisible.store(visible, std::memory_order_relaxed);
		return visible;
	}

	void DrawMotionWarp(
		const RE::NiPoint3& a_origin,
		const RE::NiPoint3& a_authoredSegment,
		const RE::NiPoint3* a_warpedSegment,
		float a_actorYaw)
	{
		auto* hud = api.load(std::memory_order_acquire);
		if (!hud || !motionWarpVisible.load(std::memory_order_relaxed)) {
			return;
		}

		const RE::NiPoint3 authoredWorld = LocalToWorld(a_authoredSegment, a_actorYaw);
		if (HasLength(authoredWorld)) {
			hud->DrawArrow(
				a_origin,
				a_origin + authoredWorld,
				10.0F,
				kMotionDuration,
				kYellow,
				kThickness);
		} else {
			hud->DrawPoint(a_origin, 6.0F, kMotionDuration, kYellow);
		}

		if (a_warpedSegment) {
			const RE::NiPoint3 warpedWorld = LocalToWorld(*a_warpedSegment, a_actorYaw);
			const RE::NiPoint3 actualOrigin =
				a_origin + RE::NiPoint3{ 0.0F, 0.0F, kActualHeightOffset };
			if (HasLength(warpedWorld)) {
				hud->DrawArrow(
					actualOrigin,
					actualOrigin + warpedWorld,
					10.0F,
					kMotionDuration,
					kCyan,
					kThickness);
			} else {
				hud->DrawPoint(actualOrigin, 6.0F, kMotionDuration, kCyan);
			}
		}
	}

	void DrawGroundProbe(
		const RE::NiPoint3& a_predictedCenter,
		const RE::NiPoint3& a_boundary,
		const RE::NiPoint3& a_rayStart,
		const RE::NiPoint3& a_rayEnd,
		const RE::NiPoint3* a_hitPosition)
	{
		auto* hud = api.load(std::memory_order_acquire);
		if (!hud || !ledgeProtectionVisible.load(std::memory_order_relaxed)) {
			return;
		}

		if (HasLength(a_boundary - a_predictedCenter)) {
			hud->DrawArrow(
				a_predictedCenter,
				a_boundary,
				8.0F,
				kGroundProbeDuration,
				kYellow,
				kThickness);
		}
		hud->DrawLine(
			a_rayStart,
			a_rayEnd,
			kGroundProbeDuration,
			a_hitPosition ? kGreen : kRed,
			kThickness);
		hud->DrawPoint(
			a_hitPosition ? *a_hitPosition : a_rayEnd,
			8.0F,
			kGroundProbeDuration,
			a_hitPosition ? kGreen : kRed);
	}
}
