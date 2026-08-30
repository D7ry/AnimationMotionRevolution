#pragma once

template <typename T>
struct Motion
{
	float time;
	T delta;
};

using Translation = Motion<RE::NiPoint3>;
using Rotation = Motion<RE::NiQuaternion>;

namespace motion_detail
{
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
}

inline std::variant<std::monostate, Translation, Rotation> ParseAnnotation(
	const RE::hkaAnnotationTrack::Annotation& a_annotation)
{
	constexpr std::string_view motionPrefix = "animmotion ";
	constexpr std::string_view rotationPrefix = "animrotation ";
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
	}

	return {};
}
