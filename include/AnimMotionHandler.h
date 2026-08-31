#pragma once

#include "Motion.h"

class AnimMotionData
{
public:
	struct TranslationRuntimeState
	{
		bool initialized{ false };
		bool wasAttacking{ false };
		bool wasWarping{ false };
		bool groundProbeInitialized{ false };
		bool wasGroundBlocked{ false };
		float previousMotionTime{ -1.0F };
		float warpScale{ -1.0F };
		std::size_t activeWarpIndex{ std::numeric_limits<std::size_t>::max() };
		RE::NiPoint3 origin{};
		float originYaw{ 0.0F };
		RE::ActorHandle target{};
		RE::NiPoint3 blockedOffset{};
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

	void Add(const Warp& a_warp)
	{
		warpList.push_back(a_warp);
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
			warpList.begin(),
			warpList.end(),
			[](const Warp& a_lhs, const Warp& a_rhs) { return a_lhs.time < a_rhs.time; });
	}

	const RE::hkaAnimation* animation{ nullptr };
	std::vector<Translation> translationList;
	std::vector<Rotation> rotationList;
	std::vector<Warp> warpList;
	std::int32_t activeCount{ 1 };
	TranslationRuntimeState translationRuntime{};
};
