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

		rayCast::enabled = ini.GetBoolValue("RayCast", "bEnable", rayCast::enabled);
		rayCast::debugDraw =
			ini.GetBoolValue("RayCast", "bDebugDraw", rayCast::debugDraw);
		rayCast::startHeight =
			ReadNonNegative(ini, "RayCast", "fStartHeight", rayCast::startHeight);
		rayCast::downwardLength =
			ReadNonNegative(ini, "RayCast", "fDownwardLength", rayCast::downwardLength);
		rayCast::minimumHorizontalDelta =
			ReadNonNegative(
				ini,
				"RayCast",
				"fMinimumHorizontalDelta",
				rayCast::minimumHorizontalDelta);

		motionWarping::enabled =
			ini.GetBoolValue("MotionWarping", "bEnable", motionWarping::enabled);
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
		motionWarping::maximumTargetAngleDegrees = std::clamp(
			ReadNonNegative(
				ini,
				"MotionWarping",
				"fMaximumTargetAngleDegrees",
				motionWarping::maximumTargetAngleDegrees),
			0.0F,
			180.0F);

		if (rayCast::downwardLength <= std::numeric_limits<float>::epsilon()) {
			logger::warn("RayCast.fDownwardLength must be positive; ray-cast limiting was disabled");
			rayCast::enabled = false;
		}

		logger::info(
			"Settings: rayCast={}, rayDebugDraw={}, motionWarping={}, extension=disabled, stopDistance={} units, maxAngle={} degrees",
			rayCast::enabled,
			rayCast::debugDraw,
			motionWarping::enabled,
			motionWarping::stopDistance,
			motionWarping::maximumTargetAngleDegrees);
	}
}
