# animwarp / animwarpend Animation Annotation Specification

## 1. Overview

`animwarp` scales one animation segment's horizontal root motion so the actor finishes that segment closer to its current combat target. It scales only the X/Y displacement supplied by `animmotion`:

- It does not rotate or redirect the authored motion.
- It does not scale Z-axis motion.
- It does not affect other animations.
- The actor needs a valid current combat target for scaling to be applied.
- An explicit `animwarp` can enable warping for non-combat animations.

## 2. Syntax

    animwarp <minimumScale> <maximumScale> [maximumAngleDegrees] [maximumDistance]
    animwarpend

All `animwarp` arguments are non-negative floating-point numbers:

- `minimumScale`: minimum permitted horizontal scale for the segment.
- `maximumScale`: maximum permitted horizontal scale for the segment.
- `maximumAngleDegrees`: optional maximum angle between authored motion and the target direction; defaults to 60 degrees.
- `maximumDistance`: optional current horizontal actor-to-target distance limit; defaults to unlimited.
- The scale range must satisfy `0 <= minimumScale <= maximumScale`.
- The angle must be between 0 and 180 degrees.

Arguments are positional. To specify a distance, the angle must also be supplied.

Examples:

    animwarp 0 1

Allows the horizontal displacement to be reduced to 0%, but not extended.

    animwarp 0.5 1.5

Allows 50%–150% of the authored horizontal displacement.

    animwarp 0 1.5 60 300

Allows 0%–150% scaling within a 60-degree angle and only while the current horizontal target distance is at most 300 Skyrim units.

    animwarpend

Disables motion warping from this timestamp until a later `animwarp`.

    animwarp 1 1

This is exactly synonymous with `animwarpend`. Angle or distance arguments do not change that meaning.

## 3. Segmentation

Each `animwarp` begins a new segment at its timestamp. The segment ends at the earliest of:

1. The next `animwarp`.
2. The next `animwarpend`.
3. The end of the `animmotion` curve.

Segments are half-open intervals: `[start, end)`. The exact ending timestamp belongs to the next segment, including when one game update crosses the boundary.

Example:

    0.20 seconds: animwarp 0 1 45 250
    0.60 seconds: animwarp 0.5 1.5 90 400
    1.00 seconds: animwarpend
    1.40 seconds: animwarp 0 1

Behavior:

- 0.00–0.20: no warping.
- 0.20–0.60: scale 0–1, maximum angle 45, maximum distance 250.
- 0.60–1.00: scale 0.5–1.5, maximum angle 90, maximum distance 400.
- 1.00–1.40: no warping.
- 1.40 onward: scale 0–1 until another control annotation or the curve ends.

When multiple controls have the same timestamp, the control occurring later in the animation data determines the following state.

## 4. Explicit controls and default attack warping

The presence of any reserved `animwarp` or `animwarpend` control makes the whole animation explicitly controlled:

- Default attack warping is suppressed for the entire animation.
- Motion before the first valid `animwarp` is not warped.
- Motion after `animwarpend` is not warped unless another `animwarp` follows.
- A valid explicit segment may warp even when the actor is not attacking.
- `bEnableForAttackAnimations = false` does not disable explicit segments.

A malformed reserved control also suppresses default attack warping and generates a log warning. This fail-closed behavior prevents a typo from unexpectedly enabling the implicit rule.

Only animations containing no `animwarp` or `animwarpend` controls can use default attack warping.

## 5. Automatic segmentation for default attack animations

When an animation has no explicit controls and default attack warping is enabled in the INI, AMR treats it as having an implicit default segment beginning at time zero. The initial defaults are:

- Minimum scale: 0.
- Maximum scale: 1.
- Maximum angle: 60 degrees.

AMR ends the current default segment and immediately starts another at every:

- Exact, case-insensitive `HitFrame` event, including `HitFrame.$payload`.
- Case-insensitive event beginning with `Collision_Add`, including values such as `Collision_Add.Node(WEAPON)`.

Example:

    0.00 seconds: implicit default animwarp
    0.70 seconds: HitFrame
    1.10 seconds: Collision_Add.Node(WEAPON)

This creates `[0.00, 0.70)`, `[0.70, 1.10)`, and `[1.10, end of animmotion]`. Each new segment recalculates against the current target, allowing consecutive hits to realign independently.

Multiple collision events at the same timestamp are merged into one boundary. Names such as `preHitFrame`, `NPCHitFrame`, and `2_HitFrame` are not mistaken for `HitFrame`.

## 6. Segment displacement and scale calculation

AMR samples cumulative motion at the exact boundaries:

    authoredSegment = animmotion(segmentEnd) - animmotion(segmentStart)
    authoredHorizontalDistance = lengthXY(authoredSegment)

On normal segment entry:

    requestedScale = max(0, horizontalTargetDistance - stopDistance) / authoredHorizontalDistance
    finalScale = clamp(requestedScale, minimumScale, maximumScale)

The final scale is applied uniformly to X/Y root motion throughout the segment. Z remains unchanged. Segment endpoints are sampled and cached once when the animation activates, and the scale remains cached while the target and eligibility conditions remain unchanged.

If the target changes, a distance gate becomes valid again, or default attack warping activates after the segment has started, AMR recalculates using only the remaining authored displacement from the current animation cursor to the same segment end. Already-consumed motion is not counted again. This is still a constant-time vector calculation.

For a segment with an authored horizontal distance of 100:

- Required movement 30 with `animwarp 0 1` produces 0.3.
- Required movement 180 with `animwarp 0 1` is clamped to 1.0.
- Required movement 180 with `animwarp 0 2` produces 1.8.
- Required movement 10 with `animwarp 0.5 1` is clamped to 0.5.

If one update crosses a segment boundary, AMR splits that update: motion before the boundary uses the old segment and motion after it uses the new segment. Multiple crossed boundaries are processed in order, preventing lost motion or artificial pauses.

## 7. Relationship to animmotion

`animwarp` does not create root motion. The animation still needs valid `animmotion` data:

    0.00 seconds: animmotion 0 0 0
    0.50 seconds: animmotion 0 50 0
    1.00 seconds: animmotion 0 100 0
    0.20 seconds: animwarp 0 1.5
    0.70 seconds: animwarpend

The explicit warp segment is `[0.20, 0.70)`, and its authored displacement is `animmotion(0.70) - animmotion(0.20)`. Motion before 0.20 and after 0.70 is unwarped.

A segment with insufficient horizontal `animmotion`, or less than the INI `fMinimumAuthoredDistance`, is not warped.

## 8. Target, direction, and distance gates

For a segment to warp:

- The actor must have a valid current combat target.
- The target must be alive.
- The actor and target must be in the same cell.
- The angle between the segment's authored motion and the target direction must not exceed the configured maximum.
- If a maximum distance was supplied, the current horizontal distance must not exceed it.

Outside the distance gate, authored motion plays unchanged. Warping may recalculate and resume after the actor re-enters the valid range.

`animwarp` scales but never rotates. If the target is behind the actor or the segment direction fails the angle gate, AMR does not steer the animation toward it. For animations that move backward and then forward, place separate `animwarp`/`animwarpend` boundaries around phases that should be calculated independently.

## 9. Z axis and ledge protection

- Motion warping scales only X/Y; authored Z-axis root motion remains unchanged.
- Any animation containing Z-axis root motion bypasses ledge-motion limiting.
- Segmentation does not change these rules.

## 10. TrueHUD debug visualization

When compiled with `AMR_ENABLE_TRUEHUD_DEBUG`:

- Page Up toggles motion-warp visualization.
- Page Down toggles ledge-protection visualization.
- These keys affect visualization only, not gameplay.

Motion-warp visualization creates a one-second snapshot using the actor's position and facing at activation:

- A snapshot is drawn when an explicit segment or generated default combat segment begins.
- If an eligible segment first starts applying later because attack state, target, angle, or distance conditions changed, another snapshot is drawn then.
- Yellow shows the segment's complete authored `animmotion` displacement.
- Elevated cyan shows the predicted scaled displacement when warping applies.
- Inactive segments, including intervals after `animwarpend`, draw no warp snapshot.
- The old target point and connector are not drawn.

The snapshot does not rotate with the actor after being drawn, and its length always represents the complete segment rather than shrinking over time.

## 11. Invalid annotations

Examples of invalid controls:

    animwarp -1 1
    animwarp 1 0.5
    animwarp abc 1
    animwarp 0
    animwarp 0 1 -10
    animwarp 0 1 181
    animwarp 0 1 60 -50
    animwarp 0 1 60 300 extra
    animwarpend extra

An invalid control does not alter the valid explicit timeline, but it still suppresses default attack warping for that animation. Check `AnimationMotionRevolution.log` for warnings.

## 12. Recommended patterns

Allow shortening during one defined phase:

    0.20 seconds: animwarp 0 1
    0.75 seconds: animwarpend

Prevent a complete stop while still prohibiting extension:

    animwarp 0.25 1

Allow modest extension:

    animwarp 0 1.25

Calculate multiple attack phases independently:

    0.20 seconds: animwarp 0 1.2
    0.70 seconds: animwarp 0 1.1
    1.15 seconds: animwarpend

Set the maximum above 1 only when needed. Excessive extension amplifies every horizontal displacement inside the segment and can produce sliding or unnaturally fast lunges.
