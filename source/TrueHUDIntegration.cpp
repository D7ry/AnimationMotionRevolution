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

		constexpr float kDuration = 0.15F;
		constexpr float kThickness = 3.0F;
		constexpr std::uint32_t kYellow = 0xFFFF00FF;
		constexpr std::uint32_t kGreen = 0x00FF00FF;
		constexpr std::uint32_t kRed = 0xFF0000FF;
		constexpr std::uint32_t kCyan = 0x00FFFFFF;
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
		const RE::NiPoint3& a_authoredDestination,
		const RE::NiPoint3& a_warpedDestination,
		const RE::NiPoint3& a_targetPosition)
	{
		auto* hud = api.load(std::memory_order_acquire);
		if (!hud || !motionWarpVisible.load(std::memory_order_relaxed)) {
			return;
		}

		hud->DrawArrow(
			a_origin, a_authoredDestination, 10.0F, kDuration, kYellow, kThickness);
		hud->DrawArrow(
			a_origin, a_warpedDestination, 10.0F, kDuration, kCyan, kThickness);
		hud->DrawLine(
			a_warpedDestination, a_targetPosition, kDuration, kRed, kThickness);
		hud->DrawPoint(a_targetPosition, 10.0F, kDuration, kRed);
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

		hud->DrawArrow(a_predictedCenter, a_boundary, 8.0F, kDuration, kYellow, kThickness);
		hud->DrawLine(
			a_rayStart,
			a_rayEnd,
			kDuration,
			a_hitPosition ? kGreen : kRed,
			kThickness);
		hud->DrawPoint(
			a_hitPosition ? *a_hitPosition : a_rayEnd,
			8.0F,
			kDuration,
			a_hitPosition ? kGreen : kRed);
	}
}
