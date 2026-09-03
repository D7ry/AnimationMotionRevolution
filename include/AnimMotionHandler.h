#pragma once

#include "Motion.h"

class AnimMotionData
{
public:
	enum class WarpSegmentKind
	{
		kInactive,
		kExplicit,
		kDefaultCombat
	};

	struct WarpMarker
	{
		float time;
		std::optional<WarpLimits> limits;
	};

	struct WarpSegment
	{
		float startTime;
		float endTime;
		RE::NiPoint3 startTranslation;
		RE::NiPoint3 endTranslation;
		WarpSegmentKind kind;
		WarpLimits limits;
	};

	struct TranslationRuntimeState
	{
		bool initialized{ false };
		bool wasAttacking{ false };
		bool wasWarping{ false };
		bool groundProbeInitialized{ false };
		bool wasGroundBlocked{ false };
		float previousMotionTime{ -1.0F };
		float warpScale{ -1.0F };
		std::size_t activeWarpSegmentIndex{ std::numeric_limits<std::size_t>::max() };
		RE::ActorHandle target{};
		RE::NiPoint3 blockedOffset{};
		RE::NiPoint3 lastAuthored{};
		RE::NiPoint3 lastWarped{};
		RE::NiPoint3 lastOutput{};
	};

	AnimMotionData() = default;

	explicit AnimMotionData(const RE::hkaAnimation* a_animation) :
		animation{ a_animation }
	{}

	void Add(const Translation& a_translation)
	{
		translationList.push_back(a_translation);
	}

	void Add(const Rotation& a_rotation)
	{
		rotationList.push_back(a_rotation);
	}

	bool Add(const Warp& a_warp)
	{
		hasExplicitWarpControl = true;
		if (!std::isfinite(a_warp.time)) {
			return false;
		}
		const bool disablesWarping =
			a_warp.delta.lowerLimit == 1.0F && a_warp.delta.upperLimit == 1.0F;
		warpMarkers.push_back({ a_warp.time,
			disablesWarping ? std::nullopt : std::optional{ a_warp.delta } });
		return true;
	}

	bool Add(const WarpEnd& a_warpEnd)
	{
		hasExplicitWarpControl = true;
		if (!std::isfinite(a_warpEnd.time)) {
			return false;
		}
		warpMarkers.push_back({ a_warpEnd.time, std::nullopt });
		return true;
	}

	void MarkExplicitWarpControl()
	{
		hasExplicitWarpControl = true;
	}

	void AddCombatWarpBoundary(float a_time)
	{
		if (std::isfinite(a_time)) {
			combatWarpBoundaries.push_back(a_time);
		}
	}

	void SortListsByTime()
	{
		std::ranges::sort(
			translationList,
			{},
			[](const Translation& a_motion) { return a_motion.time; });
		std::ranges::sort(
			rotationList,
			{},
			[](const Rotation& a_motion) { return a_motion.time; });
		std::stable_sort(
			warpMarkers.begin(),
			warpMarkers.end(),
			[](const WarpMarker& a_lhs, const WarpMarker& a_rhs) {
				return a_lhs.time < a_rhs.time;
			});
		std::ranges::sort(combatWarpBoundaries);
		std::vector<float> uniqueCombatBoundaries;
		uniqueCombatBoundaries.reserve(combatWarpBoundaries.size());
		for (const float boundary : combatWarpBoundaries) {
			if (uniqueCombatBoundaries.empty() ||
				std::abs(boundary - uniqueCombatBoundaries.back()) > 1.0e-4F) {
				uniqueCombatBoundaries.push_back(boundary);
			}
		}
		combatWarpBoundaries = std::move(uniqueCombatBoundaries);
		BuildWarpSegments();
	}

	const RE::hkaAnimation* animation{ nullptr };
	std::vector<Translation> translationList;
	std::vector<Rotation> rotationList;
	std::vector<WarpMarker> warpMarkers;
	std::vector<float> combatWarpBoundaries;
	std::vector<WarpSegment> warpSegments;
	bool hasExplicitWarpControl{ false };
	RE::NiPoint3 initialTranslation{};
	std::int32_t activeCount{ 1 };
	TranslationRuntimeState translationRuntime{};

private:
	RE::NiPoint3 SampleTranslation(float a_time) const
	{
		if (translationList.empty()) {
			return {};
		}

		const float time = std::clamp(a_time, 0.0F, translationList.back().time);
		const auto current = std::ranges::lower_bound(
			translationList,
			time,
			{},
			[](const Translation& a_motion) { return a_motion.time; });
		if (current == translationList.end()) {
			return translationList.back().delta;
		}

		const auto index = static_cast<std::size_t>(current - translationList.begin());
		const RE::NiPoint3 previousDelta = index > 0 ? translationList[index - 1].delta : RE::NiPoint3{};
		const float previousTime = index > 0 ? translationList[index - 1].time : 0.0F;
		const float duration = current->time - previousTime;
		const float progress = duration > 1.0e-4F ?
								   (time - previousTime) / duration :
								   1.0F;
		return current->delta * progress + previousDelta * (1.0F - progress);
	}

	void AddWarpSegment(
		float a_startTime,
		float a_endTime,
		WarpSegmentKind a_kind,
		const WarpLimits& a_limits = {})
	{
		if (a_endTime <= a_startTime) {
			return;
		}
		warpSegments.push_back({ a_startTime,
			a_endTime,
			SampleTranslation(a_startTime),
			SampleTranslation(a_endTime),
			a_kind,
			a_limits });
	}

	void BuildWarpSegments()
	{
		warpSegments.clear();
		if (translationList.empty()) {
			initialTranslation = {};
			return;
		}
		initialTranslation = SampleTranslation(0.0F);

		const float duration = translationList.back().time;
		if (duration <= 0.0F) {
			return;
		}

		if (hasExplicitWarpControl) {
			float segmentStart = 0.0F;
			auto kind = WarpSegmentKind::kInactive;
			WarpLimits limits{};
			for (const auto& marker : warpMarkers) {
				const float markerTime = std::clamp(marker.time, 0.0F, duration);
				AddWarpSegment(segmentStart, markerTime, kind, limits);
				segmentStart = markerTime;
				if (marker.limits) {
					kind = WarpSegmentKind::kExplicit;
					limits = *marker.limits;
				} else {
					kind = WarpSegmentKind::kInactive;
				}
			}
			AddWarpSegment(segmentStart, duration, kind, limits);
			return;
		}

		float segmentStart = 0.0F;
		for (const float boundary : combatWarpBoundaries) {
			const float boundaryTime = std::clamp(boundary, 0.0F, duration);
			AddWarpSegment(
				segmentStart,
				boundaryTime,
				WarpSegmentKind::kDefaultCombat);
			segmentStart = boundaryTime;
		}
		AddWarpSegment(segmentStart, duration, WarpSegmentKind::kDefaultCombat);
	}
};
