#pragma once

namespace truehud
{
	void Initialize();
	bool ToggleMotionWarpVisibility();
	bool ToggleLedgeProtectionVisibility();

	void DrawMotionWarp(
		const RE::NiPoint3& a_origin,
		const RE::NiPoint3& a_authoredSegment,
		const RE::NiPoint3* a_warpedSegment,
		float a_actorYaw);

	void DrawGroundProbe(
		const RE::NiPoint3& a_predictedCenter,
		const RE::NiPoint3& a_boundary,
		const RE::NiPoint3& a_rayStart,
		const RE::NiPoint3& a_rayEnd,
		const RE::NiPoint3* a_hitPosition);
}
