#pragma once

template <typename T>
struct Motion
{
	float time;
	T delta;
};

using Translation = Motion<RE::NiPoint3>;
using Rotation = Motion<RE::NiQuaternion>;

struct WarpLimits
{
	float lowerLimit{ 0.0F };
	float upperLimit{ 1.0F };
	float maximumAngleDegrees{ 60.0F };
	float maximumDistance{ std::numeric_limits<float>::infinity() };
};

using Warp = Motion<WarpLimits>;

struct WarpEnd
{
	float time;
};

namespace motion_detail
{
	inline bool HasExactToken(std::string_view a_text, std::string_view a_token)
	{
		return a_text.starts_with(a_token) &&
			   (a_text.size() == a_token.size() || a_text[a_token.size()] == ' ' ||
				   a_text[a_token.size()] == '\t');
	}

	inline bool ParseFloat(std::string_view& a_text, float& a_value)
	{
		const auto first = a_text.find_first_not_of(" \t");
		if (first == std::string_view::npos) {
			return false;
		}
		a_text.remove_prefix(first);

		const auto tokenEnd = a_text.find_first_of(" \t");
		const auto token = a_text.substr(0, tokenEnd);
		const auto result = std::from_chars(token.data(), token.data() + token.size(), a_value);
		if (result.ec != std::errc{} || result.ptr != token.data() + token.size() ||
			!std::isfinite(a_value)) {
			return false;
		}

		if (tokenEnd == std::string_view::npos) {
			a_text = {};
		} else {
			a_text.remove_prefix(tokenEnd);
		}
		return true;
	}

	inline bool HasValue(std::string_view a_text)
	{
		return a_text.find_first_not_of(" \t") != std::string_view::npos;
	}
}

inline bool IsWarpControlAnnotation(std::string_view a_text)
{
	return motion_detail::HasExactToken(a_text, "animwarp") ||
		   motion_detail::HasExactToken(a_text, "animwarpend");
}

inline std::variant<std::monostate, Translation, Rotation, Warp, WarpEnd> ParseAnnotation(
	const RE::hkaAnnotationTrack::Annotation& a_annotation)
{
	constexpr std::string_view motionPrefix = "animmotion ";
	constexpr std::string_view rotationPrefix = "animrotation ";
	constexpr std::string_view warpToken = "animwarp";
	constexpr std::string_view warpEndToken = "animwarpend";
	const std::string_view text{ a_annotation.text.c_str() };

	if (text.starts_with(motionPrefix)) {
		auto values = text.substr(motionPrefix.size());
		RE::NiPoint3 translation{};
		if (motion_detail::ParseFloat(values, translation.x) &&
			motion_detail::ParseFloat(values, translation.y) &&
			motion_detail::ParseFloat(values, translation.z)) {
			return Translation{ a_annotation.time, translation };
		}
	} else if (text.starts_with(rotationPrefix)) {
		auto values = text.substr(rotationPrefix.size());
		float yawDegrees = 0.0F;
		if (!motion_detail::ParseFloat(values, yawDegrees)) {
			return {};
		}

		const float yaw = yawDegrees * std::numbers::pi_v<float> / 180.0F;
		return Rotation{
			a_annotation.time,
			RE::NiQuaternion{
				std::cos(yaw * 0.5F),
				0.0F,
				0.0F,
				std::sin(yaw * 0.5F) }
		};
	} else if (motion_detail::HasExactToken(text, warpToken)) {
		auto values = text.substr(warpToken.size());
		WarpLimits limits{};
		if (motion_detail::ParseFloat(values, limits.lowerLimit) &&
			motion_detail::ParseFloat(values, limits.upperLimit) &&
			limits.lowerLimit >= 0.0F && limits.upperLimit >= limits.lowerLimit) {
			if (motion_detail::HasValue(values)) {
				if (!motion_detail::ParseFloat(values, limits.maximumAngleDegrees) ||
					limits.maximumAngleDegrees < 0.0F ||
					limits.maximumAngleDegrees > 180.0F) {
					return {};
				}
			}
			if (motion_detail::HasValue(values)) {
				if (!motion_detail::ParseFloat(values, limits.maximumDistance) ||
					limits.maximumDistance < 0.0F) {
					return {};
				}
			}
			if (motion_detail::HasValue(values)) {
				return {};
			}
			return Warp{ a_annotation.time, limits };
		}
	} else if (motion_detail::HasExactToken(text, warpEndToken) &&
			   !motion_detail::HasValue(text.substr(warpEndToken.size()))) {
		return WarpEnd{ a_annotation.time };
	}

	return {};
}

inline bool IsCombatWarpBoundary(std::string_view a_text)
{
	constexpr std::string_view hitFrame = "hitframe";
	constexpr std::string_view collisionAdd = "collision_add";

	const auto first = a_text.find_first_not_of(" \t");
	if (first == std::string_view::npos) {
		return false;
	}
	a_text.remove_prefix(first);

	auto equalsIgnoreCase = [](std::string_view a_value, std::string_view a_expected) {
		return a_value.size() == a_expected.size() &&
			   std::ranges::equal(
				   a_value,
				   a_expected,
				   [](char a_lhs, char a_rhs) {
					   return static_cast<char>(std::tolower(static_cast<unsigned char>(a_lhs))) ==
							  static_cast<char>(std::tolower(static_cast<unsigned char>(a_rhs)));
				   });
	};

	auto startsWithIgnoreCase = [&equalsIgnoreCase](
									std::string_view a_value,
									std::string_view a_expected) {
		return a_value.size() >= a_expected.size() &&
			   equalsIgnoreCase(a_value.substr(0, a_expected.size()), a_expected);
	};

	const auto eventEnd = a_text.find('.');
	auto eventName = a_text.substr(0, eventEnd);
	while (!eventName.empty() &&
		   (eventName.back() == ' ' || eventName.back() == '\t')) {
		eventName.remove_suffix(1);
	}

	return equalsIgnoreCase(eventName, hitFrame) ||
		   startsWithIgnoreCase(a_text, collisionAdd);
}
