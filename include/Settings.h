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

	namespace rayCast
	{
		inline bool enabled = true;
		inline bool debugDraw = true;
		inline float startHeight = 50.0F;
		inline float downwardLength = 200.0F;
		inline float minimumHorizontalDelta = 0.10F;
	}

	namespace motionWarping
	{
		inline bool enabled = true;
		inline float stopDistance = 0.0F;
		inline float minimumAuthoredDistance = 1.0F;
		inline float maximumTargetAngleDegrees = 60.0F;
	}
}
