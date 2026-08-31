#pragma once

namespace SKSE::log
{
	using level = spdlog::level::level_enum;
}
namespace logger = SKSE::log;

namespace settings
{
	void Init(std::string_view a_iniFileName);

	namespace debug
	{
		inline logger::level logLevel = logger::level::info;
	}

	namespace edgeProtection
	{
		inline bool enableForAttackAnimations = true;
		inline bool debugDraw = true;
		inline float startHeight = 50.0F;
		inline float downwardRange = 200.0F;
		inline float minimumHorizontalDelta = 0.10F;
	}

	namespace motionWarping
	{
		inline bool enableForAttackAnimations = true;
		inline float defaultMinimumScale = 0.0F;
		inline float defaultMaximumScale = 1.0F;
		inline float defaultMaximumAngleDegrees = 60.0F;
		inline float stopDistance = 0.0F;
		inline float minimumAuthoredDistance = 1.0F;
	}
}
