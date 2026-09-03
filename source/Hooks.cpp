#include "Hooks.h"

#include "AnimMotionHandler.h"
#include "Settings.h"
#ifdef AMR_ENABLE_TRUEHUD_DEBUG
#include "TrueHUDIntegration.h"
#endif

#include "utils/Logger.h"
#include "utils/Trampoline.h"

#include "RE/B/bhkCharProxyController.h"
#include "RE/B/bhkCharRigidBodyController.h"
#include "RE/B/bhkPickData.h"
#include "RE/C/CFilter.h"
#include "RE/H/hkpCharacterProxy.h"
#include "RE/H/hkpCharacterRigidBody.h"
#include "RE/H/hkpConvexVerticesShape.h"
#include "RE/H/hkpListShape.h"
#include "RE/M/MotionDataContainer.h"

namespace hooks
{
	namespace
	{
		constexpr float kMotionTimeEpsilon = 1.0e-4F;
		constexpr float kVectorEpsilon = 1.0e-5F;

		class CharacterClipAnimMotionMap
		{
		public:
			static CharacterClipAnimMotionMap* GetSingleton()
			{
				static CharacterClipAnimMotionMap singleton;
				return std::addressof(singleton);
			}

			template <typename StringT>
			void Add(
				const RE::hkbCharacter* a_hkbCharacter,
				const StringT& a_clipName,
				AnimMotionData a_animMotionData)
			{
				const std::string clipName{ a_clipName.c_str() };
				try {
					data[a_hkbCharacter][clipName] = std::move(a_animMotionData);
				} catch (const std::exception& e) {
					logger::warn(
						"Exception while tracking animation motion: {}; clearing the cache",
						e.what());
					logger::flush();
					data.clear();
				}
			}

			template <typename StringT>
			AnimMotionData* Get(const RE::hkbCharacter* a_hkbCharacter, const StringT& a_clipName)
			{
				const auto characterIt = data.find(a_hkbCharacter);
				if (characterIt == data.end()) {
					return nullptr;
				}

				const auto clipIt = characterIt->second.find(std::string{ a_clipName.c_str() });
				return clipIt != characterIt->second.end() ? std::addressof(clipIt->second) : nullptr;
			}

			template <typename StringT>
			void Remove(const RE::hkbCharacter* a_hkbCharacter, const StringT& a_clipName)
			{
				const auto characterIt = data.find(a_hkbCharacter);
				if (characterIt == data.end()) {
					return;
				}

				characterIt->second.erase(std::string{ a_clipName.c_str() });
				if (characterIt->second.empty()) {
					data.erase(characterIt);
				}
			}

			RE::BSSpinLock lock;

		private:
			std::map<const RE::hkbCharacter*, std::map<std::string, AnimMotionData>> data;
		};

		REL::Relocation<std::uint32_t (*)(const RE::hkbClipGenerator*)> g_computeStartTime;
		REL::Relocation<void (*)(const RE::hkbClipGenerator*)> g_resetIgnoreStartTime;

		REL::Relocation<void (*)(std::uintptr_t*, float, RE::NiPoint3&)> g_processTranslationData{
			REL::RelocationID{ 31812, 32582 }
		};
		REL::Relocation<void (*)(std::uintptr_t*, float, RE::NiQuaternion&)> g_processRotationData{
			REL::RelocationID{ 31813, 32583 }
		};
		REL::Relocation<void (*)(
			RE::NiQuaternion&,
			float,
			const RE::NiQuaternion&,
			const RE::NiQuaternion&)>
			g_interpolateRotation{ REL::RelocationID{ 69459, 70836 } };

		const RE::hkaAnimation* GetBoundAnimation(const RE::hkbClipGenerator* a_clip)
		{
			return a_clip && a_clip->binding && a_clip->binding->animation ?
					   a_clip->binding->animation.get() :
					   nullptr;
		}

		std::uint32_t ComputeStartTimeHook(
			const RE::hkbClipGenerator* a_clip,
			const RE::hkbContext* a_context)
		{
			auto* motionMap = CharacterClipAnimMotionMap::GetSingleton();
			RE::BSSpinLockGuard lock{ motionMap->lock };

			const auto* boundAnimation = GetBoundAnimation(a_clip);
			const auto* hkbCharacter = a_context ? a_context->character : nullptr;
			if (!boundAnimation || !hkbCharacter) {
				return g_computeStartTime(a_clip);
			}

			auto* existing = motionMap->Get(hkbCharacter, a_clip->name);
			if (existing && existing->animation == boundAnimation) {
				++existing->activeCount;
				return g_computeStartTime(a_clip);
			}

			AnimMotionData parsed{ boundAnimation };
			for (const auto& annotationTrack : boundAnimation->annotationTracks) {
				for (const auto& annotation : annotationTrack.annotations) {
					const std::string_view annotationText{ annotation.text.c_str() };
					const bool isWarpControl = IsWarpControlAnnotation(annotationText);
					if (isWarpControl) {
						parsed.MarkExplicitWarpControl();
					}
					auto parsedAnnotation = ParseAnnotation(annotation);
					if (const auto* warp = std::get_if<Warp>(&parsedAnnotation)) {
						if (!parsed.Add(*warp)) {
							logger::warn(
								"Ignoring warp control with a non-finite timestamp in animation '{}': '{}'",
								a_clip->animationName.c_str(),
								annotation.text.c_str());
						}
					} else if (const auto* warpEnd = std::get_if<WarpEnd>(&parsedAnnotation)) {
						if (!parsed.Add(*warpEnd)) {
							logger::warn(
								"Ignoring warp control with a non-finite timestamp in animation '{}': '{}'",
								a_clip->animationName.c_str(),
								annotation.text.c_str());
						}
					} else if (isWarpControl) {
						logger::warn(
							"Ignoring malformed warp control in animation '{}' at {}: '{}'",
							a_clip->animationName.c_str(),
							annotation.time,
							annotation.text.c_str());
					}
					if (IsCombatWarpBoundary(annotationText)) {
						parsed.AddCombatWarpBoundary(annotation.time);
					}
				}
			}
			for (const auto& annotationTrack : boundAnimation->annotationTracks) {
				for (const auto& annotation : annotationTrack.annotations) {
					auto parsedAnnotation = ParseAnnotation(annotation);
					if (const auto* translation = std::get_if<Translation>(&parsedAnnotation)) {
						parsed.Add(*translation);
					} else if (const auto* rotation = std::get_if<Rotation>(&parsedAnnotation)) {
						parsed.Add(*rotation);
					}
				}

				if (!parsed.translationList.empty() || !parsed.rotationList.empty()) {
					break;
				}
			}

			if (!parsed.translationList.empty() || !parsed.rotationList.empty()) {
				parsed.SortListsByTime();
				if (!parsed.translationList.empty()) {
					const float motionEndTime = parsed.translationList.back().time;
					for (const auto& marker : parsed.warpMarkers) {
						if (marker.time < 0.0F || marker.time > motionEndTime) {
							logger::warn(
								"Warp control in animation '{}' at {} is outside animmotion range 0..{} and is clamped",
								a_clip->animationName.c_str(),
								marker.time,
								motionEndTime);
						}
					}
				}
				logger::info(
					"[AMR-DIAG][activate] clip='{}' animation='{}' character={} duration={} tracks={} translationKeys={} rotationKeys={} warpMarkers={} combatBoundaries={} warpSegments={} explicitWarpTimeline={} translationEnd=({}, {}, {})",
					a_clip->name.c_str(),
					a_clip->animationName.c_str(),
					static_cast<const void*>(hkbCharacter),
					boundAnimation->duration,
					boundAnimation->annotationTracks.size(),
					parsed.translationList.size(),
					parsed.rotationList.size(),
					parsed.warpMarkers.size(),
					parsed.combatWarpBoundaries.size(),
					parsed.warpSegments.size(),
					parsed.hasExplicitWarpControl,
					parsed.translationList.empty() ? 0.0F : parsed.translationList.back().delta.x,
					parsed.translationList.empty() ? 0.0F : parsed.translationList.back().delta.y,
					parsed.translationList.empty() ? 0.0F : parsed.translationList.back().delta.z);
				for (std::size_t index = 0; index < parsed.warpSegments.size(); ++index) {
					const auto& segment = parsed.warpSegments[index];
					const auto segmentMotion = segment.endTranslation - segment.startTranslation;
					const char* kind = "inactive";
					if (segment.kind == AnimMotionData::WarpSegmentKind::kExplicit) {
						kind = "explicit";
					} else if (segment.kind == AnimMotionData::WarpSegmentKind::kDefaultCombat) {
						kind = "default-combat";
					}
					const WarpLimits displayedLimits =
						segment.kind == AnimMotionData::WarpSegmentKind::kDefaultCombat ?
							WarpLimits{
								.lowerLimit = settings::motionWarping::defaultMinimumScale,
								.upperLimit = settings::motionWarping::defaultMaximumScale,
								.maximumAngleDegrees =
									settings::motionWarping::defaultMaximumAngleDegrees,
								.maximumDistance = std::numeric_limits<float>::infinity()
							} :
							segment.limits;
					logger::info(
						"[AMR-DIAG][segment] clip='{}' index={} kind={} time=({}, {}) motion=({}, {}, {}) limits=({}, {}) angle={} distance={}",
						a_clip->name.c_str(),
						index,
						kind,
						segment.startTime,
						segment.endTime,
						segmentMotion.x,
						segmentMotion.y,
						segmentMotion.z,
						displayedLimits.lowerLimit,
						displayedLimits.upperLimit,
						displayedLimits.maximumAngleDegrees,
						displayedLimits.maximumDistance);
				}

				if (!parsed.translationList.empty() &&
					std::abs(parsed.translationList.back().time - boundAnimation->duration) >
						kMotionTimeEpsilon) {
					logger::warn(
						"Animation {} ends at {}, while custom translation ends at {}",
						a_clip->animationName.c_str(),
						boundAnimation->duration,
						parsed.translationList.back().time);
				}
				if (!parsed.rotationList.empty() &&
					std::abs(parsed.rotationList.back().time - boundAnimation->duration) >
						kMotionTimeEpsilon) {
					logger::warn(
						"Animation {} ends at {}, while custom rotation ends at {}",
						a_clip->animationName.c_str(),
						boundAnimation->duration,
						parsed.rotationList.back().time);
				}

				motionMap->Add(hkbCharacter, a_clip->name, std::move(parsed));
			}

			return g_computeStartTime(a_clip);
		}

		void ResetIgnoreStartTimeHook(
			const RE::hkbClipGenerator* a_clip,
			const RE::hkbContext* a_context)
		{
			auto* motionMap = CharacterClipAnimMotionMap::GetSingleton();
			RE::BSSpinLockGuard lock{ motionMap->lock };

			const auto* boundAnimation = GetBoundAnimation(a_clip);
			const auto* hkbCharacter = a_context ? a_context->character : nullptr;
			if (boundAnimation && hkbCharacter) {
				auto* motionData = motionMap->Get(hkbCharacter, a_clip->name);
				if (motionData && motionData->animation == boundAnimation) {
					--motionData->activeCount;
					if (motionData->activeCount <= 0) {
						motionMap->Remove(hkbCharacter, a_clip->name);
					}
				}
			}

			g_resetIgnoreStartTime(a_clip);
		}

		RE::hkbCharacter* GetHkbCharacter(RE::Character* a_character)
		{
			if (!a_character) {
				return nullptr;
			}

			RE::BSAnimationGraphManagerPtr manager;
			if (!a_character->GetAnimationGraphManager(manager) || !manager) {
				return nullptr;
			}

			const auto activeGraph = manager->GetRuntimeData().activeGraph;
			if (activeGraph >= manager->graphs.size()) {
				return nullptr;
			}

			const auto graph = manager->graphs[activeGraph];
			return graph ? std::addressof(graph->characterInstance) : nullptr;
		}

		bool SampleTranslation(
			const std::vector<Translation>& a_motion,
			float a_motionTime,
			RE::NiPoint3& a_translation)
		{
			if (a_motion.empty()) {
				return false;
			}

			const float currentTime = std::clamp(a_motionTime, 0.0F, a_motion.back().time);
			for (std::size_t index = 0; index < a_motion.size(); ++index) {
				const auto& current = a_motion[index];
				if (currentTime <= current.time) {
					const auto& previousDelta =
						index > 0 ? a_motion[index - 1].delta : RE::NiPoint3{};
					const float previousTime = index > 0 ? a_motion[index - 1].time : 0.0F;
					const float duration = current.time - previousTime;
					const float progress = duration > kMotionTimeEpsilon ?
											   (currentTime - previousTime) / duration :
											   1.0F;
					a_translation =
						current.delta * progress + previousDelta * (1.0F - progress);
					return true;
				}
			}

			a_translation = a_motion.back().delta;
			return true;
		}

		bool SampleRotation(
			const std::vector<Rotation>& a_motion,
			float a_motionTime,
			RE::NiQuaternion& a_rotation)
		{
			if (a_motion.empty()) {
				return false;
			}

			const float currentTime = std::clamp(a_motionTime, 0.0F, a_motion.back().time);
			for (std::size_t index = 0; index < a_motion.size(); ++index) {
				const auto& current = a_motion[index];
				if (currentTime <= current.time) {
					const RE::NiQuaternion previous =
						index > 0 ? a_motion[index - 1].delta :
									RE::NiQuaternion{ 1.0F, 0.0F, 0.0F, 0.0F };
					const float previousTime = index > 0 ? a_motion[index - 1].time : 0.0F;
					const float duration = current.time - previousTime;
					const float progress = duration > kMotionTimeEpsilon ?
											   (currentTime - previousTime) / duration :
											   1.0F;
					g_interpolateRotation(a_rotation, progress, previous, current.delta);
					return true;
				}
			}

			a_rotation = a_motion.back().delta;
			return true;
		}

		RE::NiPoint3 LocalToWorld(const RE::NiPoint3& a_local, float a_yaw)
		{
			const float sine = std::sin(a_yaw);
			const float cosine = std::cos(a_yaw);
			return {
				a_local.x * cosine + a_local.y * sine,
				-a_local.x * sine + a_local.y * cosine,
				a_local.z
			};
		}

		float HorizontalLength(const RE::NiPoint3& a_vector)
		{
			return std::hypot(a_vector.x, a_vector.y);
		}

		WarpLimits GetDefaultWarpLimits()
		{
			return {
				.lowerLimit = settings::motionWarping::defaultMinimumScale,
				.upperLimit = settings::motionWarping::defaultMaximumScale,
				.maximumAngleDegrees =
					settings::motionWarping::defaultMaximumAngleDegrees,
				.maximumDistance = std::numeric_limits<float>::infinity()
			};
		}

		bool HasVerticalMotion(const std::vector<Translation>& a_motion)
		{
			return std::any_of(
				a_motion.begin(),
				a_motion.end(),
				[](const Translation& a_key) {
					return std::abs(a_key.delta.z) > kVectorEpsilon;
				});
		}

		struct ActiveWarpSegment
		{
			const AnimMotionData::WarpSegment* segment{ nullptr };
			std::size_t index{ std::numeric_limits<std::size_t>::max() };
		};

		ActiveWarpSegment GetActiveWarpSegment(
			const std::vector<AnimMotionData::WarpSegment>& a_segments,
			float a_motionTime)
		{
			const auto found = std::ranges::upper_bound(
				a_segments,
				a_motionTime,
				{},
				[](const AnimMotionData::WarpSegment& a_segment) {
					return a_segment.startTime;
				});
			if (found == a_segments.begin()) {
				return {};
			}

			const auto segment = std::prev(found);
			// Segments are half-open. A marker at this exact timestamp owns the
			// frame, while the final endpoint has no active segment.
			if (a_motionTime < segment->startTime || a_motionTime >= segment->endTime) {
				return {};
			}

			return {
				std::addressof(*segment),
				static_cast<std::size_t>(segment - a_segments.begin())
			};
		}

		void ResetTranslationRuntime(
			AnimMotionData::TranslationRuntimeState& a_state,
			RE::Character* a_character)
		{
			a_state = {};
			a_state.initialized = true;
			a_state.wasAttacking = a_character->IsAttacking();
			a_state.previousMotionTime = -1.0F;
			a_state.target = a_character->GetActorRuntimeData().currentCombatTarget;
		}

		struct WarpEvaluation
		{
			bool applied{ false };
			float scale{ 1.0F };
			RE::ActorHandle target{};
		};

		WarpEvaluation EvaluateWarpSegment(
			const AnimMotionData::WarpSegment& a_segment,
			std::size_t a_segmentIndex,
			const AnimMotionData::TranslationRuntimeState& a_state,
			RE::Character* a_character,
			const RE::NiPoint3& a_origin,
			const RE::NiPoint3& a_authoredCursor,
			float a_originYaw,
			bool a_allowCachedScale)
		{
			WarpEvaluation result{};
			const bool animationOptIn =
				a_segment.kind == AnimMotionData::WarpSegmentKind::kExplicit;
			const bool defaultCombatWarp =
				a_segment.kind == AnimMotionData::WarpSegmentKind::kDefaultCombat &&
				a_character->IsAttacking() &&
				settings::motionWarping::enableForAttackAnimations;
			if (!animationOptIn && !defaultCombatWarp) {
				return result;
			}

			const WarpLimits limits = animationOptIn ? a_segment.limits : GetDefaultWarpLimits();
			result.target = a_character->GetActorRuntimeData().currentCombatTarget;
			auto target = result.target.get();
			if (!target || target->IsDead() ||
				target->GetParentCell() != a_character->GetParentCell()) {
				return result;
			}

			const RE::NiPoint3 targetOffset = target->GetPosition() - a_origin;
			const float targetDistance = HorizontalLength(targetOffset);
			if (targetDistance > limits.maximumDistance) {
				return result;
			}

			if (a_allowCachedScale && a_state.wasWarping && a_state.warpScale >= 0.0F &&
				a_state.activeWarpSegmentIndex == a_segmentIndex &&
				a_state.target == result.target) {
				result.applied = true;
				result.scale = a_state.warpScale;
				return result;
			}

			// The exact segment end was sampled once during clip activation, and the
			// current authored cursor is already available. A normal boundary uses the
			// full segment; a late reactivation uses only what remains. Both are O(1).
			const RE::NiPoint3 authoredWindow =
				a_segment.endTranslation - a_authoredCursor;
			const float authoredDistance = HorizontalLength(authoredWindow);
			if (authoredDistance < settings::motionWarping::minimumAuthoredDistance ||
				authoredDistance <= kVectorEpsilon) {
				return result;
			}

			const RE::NiPoint3 authoredWorld = LocalToWorld(authoredWindow, a_originYaw);
			if (targetDistance > kVectorEpsilon) {
				const float cosine = std::clamp(
					(authoredWorld.x * targetOffset.x + authoredWorld.y * targetOffset.y) /
						(authoredDistance * targetDistance),
					-1.0F,
					1.0F);
				const float angle =
					std::acos(cosine) * 180.0F / std::numbers::pi_v<float>;
				if (angle > limits.maximumAngleDegrees) {
					return result;
				}
			}

			const float desiredDistance = std::max(
				0.0F,
				targetDistance - settings::motionWarping::stopDistance);
			const float requestedScale = desiredDistance / authoredDistance;
			result.applied = true;
			result.scale = std::clamp(
				requestedScale,
				limits.lowerLimit,
				limits.upperLimit);
			logger::info(
				"[AMR-DIAG][warp-scale] actor=0x{:08X} target=0x{:08X} explicit={} segmentIndex={} segment=({}, {}) limits=({}, {}) maximumAngle={} maximumDistance={} targetDistance={} authoredWindowDistance={} desiredDistance={} requestedScale={} scale={}",
				a_character->GetFormID(),
				target->GetFormID(),
				animationOptIn,
				a_segmentIndex,
				a_segment.startTime,
				a_segment.endTime,
				limits.lowerLimit,
				limits.upperLimit,
				limits.maximumAngleDegrees,
				limits.maximumDistance,
				targetDistance,
				authoredDistance,
				desiredDistance,
				requestedScale,
				result.scale);
			return result;
		}

		RE::NiPoint3 ApplyWarpEvaluation(
			RE::NiPoint3 a_delta,
			const WarpEvaluation& a_evaluation)
		{
			if (a_evaluation.applied) {
				a_delta.x *= a_evaluation.scale;
				a_delta.y *= a_evaluation.scale;
			}
			return a_delta;
		}

		RE::NiPoint3 WarpTranslation(
			const RE::NiPoint3& a_translation,
			const AnimMotionData& a_motionData,
			float a_motionTime,
			AnimMotionData::TranslationRuntimeState& a_state,
			RE::Character* a_character,
			ActiveWarpSegment& a_activeSegment,
			bool& a_warpApplied,
			bool& a_warpSegmentChanged)
		{
			const auto previousSegmentIndex = a_state.activeWarpSegmentIndex;
			a_activeSegment = GetActiveWarpSegment(a_motionData.warpSegments, a_motionTime);
			a_warpSegmentChanged = a_activeSegment.index != previousSegmentIndex;

			const float currentTime = std::max(0.0F, a_motionTime);
			float timeCursor = a_state.previousMotionTime >= 0.0F ?
								   std::max(0.0F, a_state.previousMotionTime) :
								   0.0F;
			const bool firstUpdate = a_state.previousMotionTime < 0.0F;
			RE::NiPoint3 authoredCursor =
				firstUpdate ? a_motionData.initialTranslation : a_state.lastAuthored;
			// A non-zero cumulative value at t=0 is a baseline, not elapsed segment
			// motion. Preserve it unscaled and integrate only changes after t=0.
			RE::NiPoint3 mappedDelta = firstUpdate ? a_motionData.initialTranslation : RE::NiPoint3{};
			const auto actorPosition = a_character->GetPosition();
			const float actorYaw = a_character->GetAngleZ();

			WarpEvaluation finalEvaluation{};
			std::size_t finalEvaluationIndex = std::numeric_limits<std::size_t>::max();
			auto segmentIt = std::ranges::upper_bound(
				a_motionData.warpSegments,
				timeCursor,
				{},
				[](const AnimMotionData::WarpSegment& a_segment) {
					return a_segment.endTime;
				});

			for (; segmentIt != a_motionData.warpSegments.end() && timeCursor < currentTime;
				 ++segmentIt) {
				if (segmentIt->endTime <= timeCursor) {
					continue;
				}
				if (segmentIt->startTime > timeCursor) {
					if (segmentIt->startTime >= currentTime) {
						break;
					}
					mappedDelta += segmentIt->startTranslation - authoredCursor;
					authoredCursor = segmentIt->startTranslation;
					timeCursor = segmentIt->startTime;
				}
				if (segmentIt->startTime >= currentTime) {
					break;
				}

				const float pieceEndTime = std::min(currentTime, segmentIt->endTime);
				if (pieceEndTime <= timeCursor) {
					continue;
				}
				const RE::NiPoint3 pieceEndTranslation =
					segmentIt->endTime <= currentTime ? segmentIt->endTranslation : a_translation;
				const auto segmentIndex =
					static_cast<std::size_t>(segmentIt - a_motionData.warpSegments.begin());
				const RE::NiPoint3 evaluationOrigin =
					actorPosition + LocalToWorld(mappedDelta, actorYaw);
				const auto evaluation = EvaluateWarpSegment(
					*segmentIt,
					segmentIndex,
					a_state,
					a_character,
					evaluationOrigin,
					authoredCursor,
					actorYaw,
					segmentIndex == previousSegmentIndex);
				mappedDelta += ApplyWarpEvaluation(
					pieceEndTranslation - authoredCursor,
					evaluation);
				authoredCursor = pieceEndTranslation;
				timeCursor = pieceEndTime;
				finalEvaluation = evaluation;
				finalEvaluationIndex = segmentIndex;
			}

			if (timeCursor < currentTime) {
				mappedDelta += a_translation - authoredCursor;
				authoredCursor = a_translation;
			}

			if (a_activeSegment.segment &&
				finalEvaluationIndex != a_activeSegment.index) {
				const RE::NiPoint3 evaluationOrigin =
					actorPosition + LocalToWorld(mappedDelta, actorYaw);
				finalEvaluation = EvaluateWarpSegment(
					*a_activeSegment.segment,
					a_activeSegment.index,
					a_state,
					a_character,
					evaluationOrigin,
					authoredCursor,
					actorYaw,
					a_activeSegment.index == previousSegmentIndex);
				finalEvaluationIndex = a_activeSegment.index;
			}

			a_warpApplied = a_activeSegment.segment && finalEvaluation.applied;
			a_state.activeWarpSegmentIndex = a_activeSegment.index;
			a_state.warpScale = a_warpApplied ? finalEvaluation.scale : -1.0F;
			a_state.target = finalEvaluation.target;
			a_state.lastAuthored = a_translation;
			return a_state.lastWarped + mappedDelta;
		}

		struct GroundProbeResult
		{
			bool hasGround{ true };
			bool hasWorld{ false };
			float startZ{ 0.0F };
			float endZ{ 0.0F };
			float hitFraction{ 1.0F };
			float worldScale{ 0.0F };
			std::uint32_t filterInfo{ 0 };
			RE::NiPoint3 hitPosition{};
		};

		struct ControllerRadius
		{
			std::uint32_t rootShapeType{ 0 };
			float halfExtentX{ 0.0F };
			float halfExtentY{ 0.0F };
			float convexMargin{ 0.0F };
			float inverseScale{ 0.0F };
			float world{ 0.0F };
		};

		ControllerRadius GetCharacterControllerRadius(RE::Character* a_character)
		{
			ControllerRadius result{};
			if (const auto* controller = a_character->GetCharController()) {
				const RE::hkpShape* shape = nullptr;
				if (const auto* proxyController =
						skyrim_cast<const RE::bhkCharProxyController*>(controller)) {
					if (const auto* proxy = proxyController->GetCharacterProxy();
						proxy && proxy->shapePhantom) {
						shape = proxy->shapePhantom->collidable.shape;
					}
				} else if (const auto* rigidController =
							   skyrim_cast<const RE::bhkCharRigidBodyController*>(controller)) {
					const auto* characterRigidBody = static_cast<const RE::hkpCharacterRigidBody*>(
						rigidController->charRigidBody.referencedObject.get());
					if (characterRigidBody && characterRigidBody->character) {
						shape = characterRigidBody->character->collidable.shape;
					}
				}

				if (!shape) {
					return result;
				}
				result.rootShapeType = static_cast<std::uint32_t>(shape->type);

				if (const auto* list = skyrim_cast<const RE::hkpListShape*>(shape);
					list && !list->childInfo.empty()) {
					shape = list->childInfo.front().shape;
				}

				if (const auto* convex =
						skyrim_cast<const RE::hkpConvexVerticesShape*>(shape)) {
					result.halfExtentX = std::abs(convex->aabbHalfExtents.quad.m128_f32[0]);
					result.halfExtentY = std::abs(convex->aabbHalfExtents.quad.m128_f32[1]);
					result.convexMargin = convex->radius;
					result.inverseScale = RE::bhkWorld::GetWorldScaleInverse();
					result.world =
						std::max(result.halfExtentX, result.halfExtentY) * result.inverseScale;
					if (std::isfinite(result.world) && result.world > kVectorEpsilon) {
						return result;
					}
				}
			}
			return {};
		}

		GroundProbeResult ProbeGroundAt(
			RE::Character* a_character,
			const RE::NiPoint3& a_position)
		{
			GroundProbeResult result{};
			const auto* cell = a_character->GetParentCell();
			auto* world = cell ? cell->GetbhkWorld() : nullptr;
			if (!world) {
				return result;
			}
			result.hasWorld = true;

			// Skyrim positions are converted to Havok space with GetWorldScale().
			// GetWorldScaleInverse() converts in the opposite direction and places
			// the ray thousands of Havok units away from the destination.
			const float scale = RE::bhkWorld::GetWorldScale();
			const float startZ = a_position.z + settings::edgeProtection::startHeight;
			const float endZ = startZ - settings::edgeProtection::downwardRange;
			result.startZ = startZ;
			result.endZ = endZ;
			result.worldScale = scale;

			RE::bhkPickData pick{};
			pick.rayInput.from = RE::hkVector4{
				a_position.x * scale,
				a_position.y * scale,
				startZ * scale,
				0.0F
			};
			pick.rayInput.to = RE::hkVector4{
				a_position.x * scale,
				a_position.y * scale,
				endZ * scale,
				0.0F
			};
			RE::CFilter actorFilter{};
			a_character->GetCollisionFilterInfo(actorFilter);
			const auto collisionGroup = static_cast<std::uint16_t>(actorFilter.filter >> 16);
			pick.rayInput.filterInfo.filter =
				(static_cast<std::uint32_t>(collisionGroup) << 16) |
				static_cast<std::uint32_t>(RE::COL_LAYER::kCharController);
			result.filterInfo = pick.rayInput.filterInfo.filter;

			world->PickObject(pick);
			result.hasGround = pick.rayOutput.HasHit();
			result.hitFraction = pick.rayOutput.hitFraction;
			if (result.hasGround) {
				result.hitPosition = {
					a_position.x,
					a_position.y,
					startZ + (endZ - startZ) * result.hitFraction
				};
			}
			return result;
		}

		void ApplyTranslationModifiers(
			AnimMotionData& a_motionData,
			float a_motionTime,
			RE::Character* a_character,
			RE::NiPoint3& a_translation,
			std::string_view a_clipName)
		{
			auto& state = a_motionData.translationRuntime;
			const bool hasVerticalMotion = HasVerticalMotion(a_motionData.translationList);
			const bool resetRuntime = !state.initialized ||
									  a_motionTime + kMotionTimeEpsilon < state.previousMotionTime;
			if (resetRuntime) {
				ResetTranslationRuntime(state, a_character);
				const auto& authoredEnd = a_motionData.translationList.back().delta;
				logger::info(
					"[AMR-DIAG][motion-start] actor=0x{:08X} clip='{}' time={} keys={} authoredEnd=({}, {}, {}) position=({}, {}, {}) verticalMotion={} attackEdgeProtection={} defaultAttackWarping={} attacking={}",
					a_character->GetFormID(),
					a_clipName,
					a_motionTime,
					a_motionData.translationList.size(),
					authoredEnd.x,
					authoredEnd.y,
					authoredEnd.z,
					a_character->GetPositionX(),
					a_character->GetPositionY(),
					a_character->GetPositionZ(),
					hasVerticalMotion,
					settings::edgeProtection::enableForAttackAnimations,
					settings::motionWarping::enableForAttackAnimations,
					a_character->IsAttacking());
			}

			if (state.previousMotionTime >= 0.0F &&
				std::abs(a_motionTime - state.previousMotionTime) <= kMotionTimeEpsilon) {
				a_translation = state.lastOutput;
				return;
			}

			const bool isAttacking = a_character->IsAttacking();
			ActiveWarpSegment activeWarpSegment{};
			bool warpApplied = false;
			bool warpSegmentChanged = false;
			const RE::NiPoint3 warped = WarpTranslation(
				a_translation,
				a_motionData,
				a_motionTime,
				state,
				a_character,
				activeWarpSegment,
				warpApplied,
				warpSegmentChanged);
#ifdef AMR_ENABLE_TRUEHUD_DEBUG
			const bool warpSegmentEnabled =
				activeWarpSegment.segment &&
				(activeWarpSegment.segment->kind ==
						 AnimMotionData::WarpSegmentKind::kExplicit ||
					(activeWarpSegment.segment->kind ==
						 AnimMotionData::WarpSegmentKind::kDefaultCombat &&
					 settings::motionWarping::enableForAttackAnimations &&
					 isAttacking));
			const bool visualizeWarpActivation =
				warpSegmentEnabled &&
				(warpSegmentChanged || (warpApplied && !state.wasWarping));
			if (visualizeWarpActivation) {
				const RE::NiPoint3 authoredSegment =
					activeWarpSegment.segment->endTranslation -
					activeWarpSegment.segment->startTranslation;
				RE::NiPoint3 warpedSegment = authoredSegment;
				if (warpApplied) {
					warpedSegment.x *= state.warpScale;
					warpedSegment.y *= state.warpScale;
				}
				const auto actorPosition = a_character->GetPosition();
				const float actorYaw = a_character->GetAngleZ();
				truehud::DrawMotionWarp(
					actorPosition,
					authoredSegment,
					warpApplied ? std::addressof(warpedSegment) : nullptr,
					actorYaw);
			}
#endif
			if (state.previousMotionTime >= 0.0F &&
				(warpApplied != state.wasWarping || warpSegmentChanged)) {
				logger::info(
					"[AMR-DIAG][warp] clip='{}' state={} attacking={} time={}",
					a_clipName,
					warpApplied ? "applied" : "inactive",
					isAttacking,
					a_motionTime);
			}
			state.wasAttacking = isAttacking;
			state.wasWarping = warpApplied;
			RE::NiPoint3 output = warped - state.blockedOffset;
			const RE::NiPoint3 localDelta = output - state.lastOutput;
			const RE::NiPoint3 intendedLocalDelta = warped - state.lastWarped;

			if (settings::edgeProtection::enableForAttackAnimations && isAttacking &&
				!hasVerticalMotion &&
				HorizontalLength(localDelta) >=
					settings::edgeProtection::minimumHorizontalDelta) {
				const RE::NiPoint3 worldDelta =
					LocalToWorld(localDelta, a_character->GetAngleZ());
				const RE::NiPoint3 predictedCenter = a_character->GetPosition() + worldDelta;
				RE::NiPoint3 boundaryDirection =
					LocalToWorld(intendedLocalDelta, a_character->GetAngleZ());
				float boundaryDirectionLength = HorizontalLength(boundaryDirection);
				if (boundaryDirectionLength <= kVectorEpsilon) {
					boundaryDirection = worldDelta;
					boundaryDirectionLength = HorizontalLength(boundaryDirection);
				}
				const auto controllerRadius = GetCharacterControllerRadius(a_character);
				RE::NiPoint3 probePosition = predictedCenter;
				if (boundaryDirectionLength > kVectorEpsilon && controllerRadius.world > 0.0F) {
					probePosition.x += boundaryDirection.x / boundaryDirectionLength * controllerRadius.world;
					probePosition.y += boundaryDirection.y / boundaryDirectionLength * controllerRadius.world;
				}

				const auto probe = ProbeGroundAt(a_character, probePosition);
#ifdef AMR_ENABLE_TRUEHUD_DEBUG
				if (settings::edgeProtection::debugDraw) {
					const RE::NiPoint3 rayStart{
						probePosition.x, probePosition.y, probe.startZ
					};
					const RE::NiPoint3 rayEnd{
						probePosition.x, probePosition.y, probe.endZ
					};
					truehud::DrawGroundProbe(
						predictedCenter,
						probePosition,
						rayStart,
						rayEnd,
						probe.hasGround ? std::addressof(probe.hitPosition) : nullptr);
				}
#endif
				const bool groundBlocked = !probe.hasGround;
				if (!state.groundProbeInitialized ||
					groundBlocked != state.wasGroundBlocked) {
					logger::info(
						"[AMR-DIAG][ground] clip='{}' state={} center=({}, {}, {}) boundary=({}, {}, {}) rootShapeType={} convexHalfExtents=({}, {}) convexMargin={} inverseScale={} controllerRadiusWorld={} rayZ=({}, {}) hitFraction={} scale={} filter=0x{:08X} world={}",
						a_clipName,
						groundBlocked ? "blocked" : "clear",
						predictedCenter.x,
						predictedCenter.y,
						predictedCenter.z,
						probePosition.x,
						probePosition.y,
						probePosition.z,
						controllerRadius.rootShapeType,
						controllerRadius.halfExtentX,
						controllerRadius.halfExtentY,
						controllerRadius.convexMargin,
						controllerRadius.inverseScale,
						controllerRadius.world,
						probe.startZ,
						probe.endZ,
						probe.hitFraction,
						probe.worldScale,
						probe.filterInfo,
						probe.hasWorld);
				}
				state.groundProbeInitialized = true;
				state.wasGroundBlocked = groundBlocked;

				if (groundBlocked) {
					state.blockedOffset.x += localDelta.x;
					state.blockedOffset.y += localDelta.y;
					output.x = state.lastOutput.x;
					output.y = state.lastOutput.y;
				}
			}

			state.previousMotionTime = a_motionTime;
			state.lastWarped = warped;
			state.lastOutput = output;
			a_translation = output;
		}

		void ProcessTranslationDataHook(
			RE::MotionDataContainer* a_container,
			float a_motionTime,
			RE::NiPoint3& a_translation,
			const RE::BSFixedString* a_clipName,
			RE::Character* a_character)
		{
			auto* motionMap = CharacterClipAnimMotionMap::GetSingleton();
			RE::BSSpinLockGuard lock{ motionMap->lock };

			auto* hkbCharacter = GetHkbCharacter(a_character);
			auto* motionData =
				hkbCharacter && a_clipName ? motionMap->Get(hkbCharacter, *a_clipName) : nullptr;

			if (motionData &&
				SampleTranslation(motionData->translationList, a_motionTime, a_translation)) {
				ApplyTranslationModifiers(
					*motionData,
					a_motionTime,
					a_character,
					a_translation,
					a_clipName->c_str());
				return;
			}

			const bool hasVanillaMotion =
				a_container->translationSegCount >
				static_cast<std::uint32_t>(a_container->IsTranslationDataAligned());
			if (hasVanillaMotion) {
				g_processTranslationData(
					std::addressof(a_container->translationDataPtr),
					a_motionTime,
					a_translation);
				return;
			}

			a_translation = {};
		}

		void ProcessRotationDataHook(
			RE::MotionDataContainer* a_container,
			float a_motionTime,
			RE::NiQuaternion& a_rotation,
			const RE::BSFixedString* a_clipName,
			RE::Character* a_character)
		{
			auto* motionMap = CharacterClipAnimMotionMap::GetSingleton();
			RE::BSSpinLockGuard lock{ motionMap->lock };

			auto* hkbCharacter = GetHkbCharacter(a_character);
			auto* motionData =
				hkbCharacter && a_clipName ? motionMap->Get(hkbCharacter, *a_clipName) : nullptr;

			if (motionData && SampleRotation(motionData->rotationList, a_motionTime, a_rotation)) {
				return;
			}

			const bool hasVanillaMotion =
				a_container->rotationSegCount >
				static_cast<std::uint32_t>(a_container->IsRotationDataAligned());
			if (hasVanillaMotion) {
				g_processRotationData(
					std::addressof(a_container->rotationDataPtr),
					a_motionTime,
					a_rotation);
				return;
			}

			a_rotation = RE::NiQuaternion{ 1.0F, 0.0F, 0.0F, 0.0F };
		}

	}

	void Install()
	{
		{
			REL::Relocation<std::uintptr_t> activate{
				REL::RelocationID{ 58602, 59252 }
			};
			const std::uintptr_t hookedAddress = activate.address() + 0x66E;

			struct Hook : Xbyak::CodeGenerator
			{
				explicit Hook(std::uintptr_t a_returnAddress)
				{
					Xbyak::Label hookLabel;
					Xbyak::Label returnLabel;
					mov(rdx, r15);
					call(ptr[rip + hookLabel]);
					jmp(ptr[rip + returnLabel]);
					L(hookLabel), dq(reinterpret_cast<std::uintptr_t>(ComputeStartTimeHook));
					L(returnLabel), dq(a_returnAddress);
				}
			};

			g_computeStartTime =
				utils::WriteBranchTrampoline<5>(hookedAddress, Hook{ hookedAddress + 5 });
		}

		{
			REL::Relocation<std::uintptr_t> deactivate{
				REL::RelocationID{ 58604, 59254 }
			};
			const std::uintptr_t hookedAddress = deactivate.address() + 0x1A;

			struct Hook : Xbyak::CodeGenerator
			{
				explicit Hook(std::uintptr_t a_returnAddress)
				{
					Xbyak::Label hookLabel;
					Xbyak::Label returnLabel;
					mov(rdx, r14);
					call(ptr[rip + hookLabel]);
					jmp(ptr[rip + returnLabel]);
					L(hookLabel), dq(reinterpret_cast<std::uintptr_t>(ResetIgnoreStartTimeHook));
					L(returnLabel), dq(a_returnAddress);
				}
			};

			g_resetIgnoreStartTime =
				utils::WriteBranchTrampoline<5>(hookedAddress, Hook{ hookedAddress + 5 });
		}

		{
			REL::Relocation<std::uintptr_t> processMotionData{
				REL::RelocationID{ 31949, 32703 }
			};
			const auto base = processMotionData.address();
			const auto translation1 = base + (REL::Module::IsAE() ? 0x298 : 0x28D);
			const auto translation2 = base + (REL::Module::IsAE() ? 0x2AA : 0x2A1);
			const auto rotation1 = base + (REL::Module::IsAE() ? 0x35C : 0x355);
			const auto rotation2 = base + (REL::Module::IsAE() ? 0x36D : 0x368);
			logger::info(
				"[AMR-DIAG][hooks] processMotionData=0x{:X} translation=(0x{:X}, 0x{:X}) rotation=(0x{:X}, 0x{:X}) runtime={}",
				base,
				translation1,
				translation2,
				rotation1,
				rotation2,
				REL::Module::IsAE() ? "AE" : "SE");

			struct Hook : Xbyak::CodeGenerator
			{
				explicit Hook(void* a_function)
				{
					Xbyak::Label hookLabel;
					sub(rsp, 0x28);
					mov(ptr[rsp + 0x20], r13);
					mov(r9, rbx);
					if (REL::Module::IsAE()) {
						sub(r9, 0x14);
					}
					call(ptr[rip + hookLabel]);
					add(rsp, 0x28);
					ret();
					L(hookLabel), dq(reinterpret_cast<std::uintptr_t>(a_function));
				}
			};

			utils::WriteCallTrampoline<5>(
				translation1,
				Hook{ reinterpret_cast<void*>(ProcessTranslationDataHook) });
			utils::WriteCallTrampoline<5>(
				translation2,
				Hook{ reinterpret_cast<void*>(ProcessTranslationDataHook) });
			utils::WriteCallTrampoline<5>(
				rotation1,
				Hook{ reinterpret_cast<void*>(ProcessRotationDataHook) });
			utils::WriteCallTrampoline<5>(
				rotation2,
				Hook{ reinterpret_cast<void*>(ProcessRotationDataHook) });
		}

		logger::info("Installed Animation Motion Revolution hooks");
	}
}
