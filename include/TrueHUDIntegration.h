#pragma once

namespace truehud
{
	void Initialize();

	void DrawMotionWarp(
		const RE::NiPoint3& a_origin,
		const RE::NiPoint3& a_authoredDestination,
		const RE::NiPoint3& a_warpedDestination,
		const RE::NiPoint3& a_targetPosition);

	void DrawGroundProbe(
		const RE::NiPoint3& a_predictedCenter,
		const RE::NiPoint3& a_boundary,
		const RE::NiPoint3& a_rayStart,
		const RE::NiPoint3& a_rayEnd,
		const RE::NiPoint3* a_hitPosition);
}
