#include "Settings.h"

#include "utils/Logger.h"

#include <SimpleIni.h>

namespace
{
	float ReadNonNegative(
		const CSimpleIniA& a_ini,
		const char* a_section,
		const char* a_key,
		float a_default)
	{
		const auto value = static_cast<float>(a_ini.GetDoubleValue(a_section, a_key, a_default));
		return std::isfinite(value) && value >= 0.0F ? value : a_default;
	}
}
namespace settings
{
	void Init(std::string_view a_iniFileName)
	{
		const auto iniPath =
			std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName;

		CSimpleIniA ini;
		if (ini.LoadFile(iniPath.string().c_str()) < 0) {
			logger::warn("Could not read {}; using default options", iniPath.string());
			return;
		}

		const auto logLevelValue = std::clamp(
			ini.GetLongValue("Debug", "uLogLevel", static_cast<long>(debug::logLevel)),
			static_cast<long>(logger::level::trace),
			static_cast<long>(logger::level::off));
		debug::logLevel = static_cast<logger::level>(logLevelValue);

		edgeProtection::enableForAttackAnimations = ini.GetBoolValue(
			"EdgeProtection",
			"bEnableForAttackAnimations",
			edgeProtection::enableForAttackAnimations);
		edgeProtection::debugDraw = ini.GetBoolValue(
			"EdgeProtection",
			"bDebugDraw",
			edgeProtection::debugDraw);
		edgeProtection::startHeight = ReadNonNegative(
			ini,
			"EdgeProtection",
			"fRaycastStartHeight",
			edgeProtection::startHeight);
		edgeProtection::downwardRange = ReadNonNegative(
			ini,
			"EdgeProtection",
			"fRaycastDownwardRange",
			edgeProtection::downwardRange);
		edgeProtection::minimumHorizontalDelta =
			ReadNonNegative(
				ini,
				"EdgeProtection",
				"fMinimumHorizontalDelta",
				edgeProtection::minimumHorizontalDelta);

		motionWarping::enableForAttackAnimations = ini.GetBoolValue(
			"MotionWarping",
			"bEnableForAttackAnimations",
			motionWarping::enableForAttackAnimations);
		const float defaultMinimumScale = ReadNonNegative(
			ini,
			"MotionWarping",
			"fDefaultMinimumScale",
			motionWarping::defaultMinimumScale);
		const float defaultMaximumScale = ReadNonNegative(
			ini,
			"MotionWarping",
			"fDefaultMaximumScale",
			motionWarping::defaultMaximumScale);
		if (defaultMaximumScale >= defaultMinimumScale) {
			motionWarping::defaultMinimumScale = defaultMinimumScale;
			motionWarping::defaultMaximumScale = defaultMaximumScale;
		} else {
			logger::warn(
				"MotionWarping default maximum scale is below its minimum; using {}..{}",
				motionWarping::defaultMinimumScale,
				motionWarping::defaultMaximumScale);
		}
		motionWarping::defaultMaximumAngleDegrees = std::clamp(
			ReadNonNegative(
				ini,
				"MotionWarping",
				"fDefaultMaximumAngleDegrees",
				motionWarping::defaultMaximumAngleDegrees),
			0.0F,
			180.0F);
		motionWarping::stopDistance =
			ReadNonNegative(
				ini,
				"MotionWarping",
				"fStopDistance",
				motionWarping::stopDistance);
		motionWarping::minimumAuthoredDistance =
			ReadNonNegative(
				ini,
				"MotionWarping",
				"fMinimumAuthoredDistance",
				motionWarping::minimumAuthoredDistance);
		if (edgeProtection::downwardRange <= std::numeric_limits<float>::epsilon()) {
			logger::warn(
				"EdgeProtection.fRaycastDownwardRange must be positive; edge protection was disabled");
			edgeProtection::enableForAttackAnimations = false;
		}

		logger::info(
			"Settings: attackWarping={} defaultScale=({}, {}) defaultAngle={} stopDistance={} attackEdgeProtection={} rayDownRange={} edgeDebugDraw={}",
			motionWarping::enableForAttackAnimations,
			motionWarping::defaultMinimumScale,
			motionWarping::defaultMaximumScale,
			motionWarping::defaultMaximumAngleDegrees,
			motionWarping::stopDistance,
			edgeProtection::enableForAttackAnimations,
			edgeProtection::downwardRange,
			edgeProtection::debugDraw);
	}
}
