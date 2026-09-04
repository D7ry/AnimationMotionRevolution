# AMR Animation and Behavior Developer Guide

> **Audience:** This document is for animation authors and behavior developers. It describes the annotation and graph-variable contract implemented by this fork of Animation Motion Revolution (AMR). It is not an end-user installation guide.

Languages: **English** | [简体中文](developer-guide.zh-CN.md) | [한국어](developer-guide.ko-KR.md)

## 1. What AMR controls

AMR reads annotations embedded in an HKX animation and can replace that clip's motion data with:

- `animmotion`: cumulative local translation;
- `animrotation`: cumulative yaw rotation;
- `animwarp` / `animwarpend`: optional, segmented scaling of horizontal `animmotion` toward the actor's current combat target.

The behavior graph must consume animation-driven motion. Configure the relevant behavior/state with the appropriate animation-driven and rotation permissions (`bAnimationDriven` and/or `bAllowRotation`). An annotation alone cannot make a behavior that ignores root motion move the actor.

AMR works on `RE::Character` instances, so the same runtime path is used for the player, NPCs, and supported creatures. A particular behavior project still needs to expose and consume the required motion data and graph variable.

## 2. Annotation format and common parser rules

An hkanno-style line consists of an annotation timestamp followed by its text:

```text
0.500000 animmotion 0 100 0
0.500000 animrotation 45
0.200000 animwarp 0 1 60 300
0.750000 animwarpend
```

The timestamp is stored separately by Havok and is measured in seconds from the start of the clip. The remaining text is what AMR parses.

Follow these authoring rules:

- Annotation names are lowercase and case-sensitive: use exactly `animmotion`, `animrotation`, `animwarp`, and `animwarpend`.
- Do not put whitespace before an AMR annotation name.
- Use ordinary ASCII spaces between values. In the current parser, `animmotion` and `animrotation` require a literal space after the name. `animwarp` and `animwarpend` also accept a tab, but relying on that difference is discouraged.
- Numeric values must be finite, period-decimal floating-point values. Scientific notation is accepted by the underlying numeric parser. `NaN` and infinity are rejected.
- Keep `animmotion` and `animrotation` timestamps within `[0, clip duration]`. Unlike warp controls, motion and rotation timestamps are not clamped.
- Give motion and rotation keys unique timestamps. The ordering of duplicate `animmotion` or `animrotation` keys at the same time is unspecified.
- Put every `animmotion` and `animrotation` key for a clip in the same annotation track. AMR uses the first track containing any valid motion or rotation key and ignores motion/rotation keys in later tracks.
- Warp controls and automatic-combat boundary events are scanned across all annotation tracks.

The current parser ignores trailing values after a valid `animmotion` or `animrotation`. Treat that as an implementation quirk, not supported syntax. By contrast, an extra argument makes `animwarp` or `animwarpend` malformed.

A malformed motion/rotation annotation is silently ignored. A malformed exact warp-control token is logged and has the fail-closed behavior described in [Explicit control and fail-closed behavior](#explicit-control-and-fail-closed-behavior).

## 3. `animmotion`: cumulative translation

### Syntax

```text
animmotion <x> <y> <z>
```

The three values are a **cumulative local displacement at the annotation timestamp**, not movement to add at that key.

- X: lateral motion;
- Y: forward/back motion, with positive Y being the actor-facing direction in the standard Skyrim/ADSF convention;
- Z: vertical motion.

Values use Skyrim animation/world-unit scale, matching the translation convention used by `animationdatasinglefile.txt` (ADSF). They are not Havok meters. Negative values are valid.

Example:

```text
0.000000 animmotion 0 0 0
0.250000 animmotion 0 20 0
0.500000 animmotion 0 80 0
0.750000 animmotion 0 100 0
```

At 0.25 seconds the cumulative position is Y=20; the next key means Y=80 at 0.50 seconds, so that interval contributes 60 units. AMR linearly interpolates each component between keys.

### Start and end keys

Recommended form:

```text
0.000000 animmotion 0 0 0
<clip-duration> animmotion <final-x> <final-y> <final-z>
```

- If the first key occurs after zero, AMR interpolates from an implicit zero vector at time zero.
- A nonzero key exactly at time zero is an immediate baseline. It is preserved without motion-warp scaling and may look like a snap, so normally use `0 0 0`.
- After the final key, the cumulative value is held constant. AMR logs a warning if the final custom-motion timestamp differs from the HKX duration.
- The end of the `animmotion` curve, not necessarily the declared HKX duration, is also the end of the motion-warp timeline.

When a valid `animmotion` curve exists, it replaces the clip's vanilla translation motion. Without one, AMR leaves available vanilla translation motion to the game; if neither custom nor vanilla translation exists, the sampled translation is zero.

## 4. `animrotation`: cumulative yaw

### Syntax

```text
animrotation <yawDegrees>
```

`animrotation` defines cumulative yaw around the vertical Z axis. It does not define pitch or roll. AMR converts each value to a quaternion and uses the game's quaternion interpolation between keys. Negative angles are valid.

Example for a full turn:

```text
0.000000 animrotation 0
0.500000 animrotation 90
0.900000 animrotation 180
1.200000 animrotation 270
1.500000 animrotation 360
```

Use intermediate keys for rotations approaching or exceeding 180 degrees. A lone `animrotation 360` has the same quaternion orientation as zero and therefore cannot describe the intended full path by itself.

As with translation, start at zero and normally put the final key at the clip duration. A valid custom rotation curve replaces the clip's vanilla rotation. Without one, available vanilla rotation is retained; if neither exists, the identity rotation is used.

`animmotion` and `animrotation` may be mixed in the same annotation track. Motion warping never modifies `animrotation`.

## 5. `animwarp` and `animwarpend`

Motion warping scales a segment's X/Y translation uniformly. It does not create motion, rotate the path, steer the actor, scale Z, or alter `animrotation`. Consequently, a clip without a valid `animmotion` translation curve cannot be warped by AMR.

### Syntax

```text
animwarp <minimumScale> <maximumScale> [maximumAngleDegrees] [maximumDistance]
animwarpend
```

Arguments are positional:

- `minimumScale`: lowest permitted X/Y scale, at least 0;
- `maximumScale`: highest permitted X/Y scale, at least `minimumScale`;
- `maximumAngleDegrees`: optional 0–180 degree gate; default 60;
- `maximumDistance`: optional nonnegative horizontal actor-to-target distance in Skyrim units; default unlimited.

To specify distance, angle must also be present.

Examples:

```text
animwarp 0 1
animwarp 0.25 1
animwarp 0 1.25
animwarp 0 1.5 60 300
animwarpend
```

`animwarp 0 1` permits complete reduction but no extension. An upper scale greater than 1 permits extension. A lower scale greater than 0 prevents the actor from fully stopping, even if already within the configured stop distance.

`animwarpend` changes the timeline to an inactive state until a later valid `animwarp`. `animwarp 1 1` is canonicalized to exactly the same inactive state even when valid optional angle or distance arguments follow it; invalid optional values still make the annotation malformed.

### Explicit segments

Each valid control owns the half-open interval beginning at its timestamp:

```text
[control time, next control time)
```

The final control ends at the last `animmotion` timestamp. The exact time of a following control belongs to the new segment. If one game update crosses one or more boundaries, AMR splits that update and applies each segment's rule to its respective part, avoiding a lost frame or artificial pause.

Example:

```text
0.200000 animwarp 0 1 45 250
0.600000 animwarp 0.5 1.5 90 400
1.000000 animwarpend
1.400000 animwarp 0 1
```

This produces:

- `[0.00, 0.20)`: inactive;
- `[0.20, 0.60)`: scale 0–1, 45 degrees, 250 units;
- `[0.60, 1.00)`: scale 0.5–1.5, 90 degrees, 400 units;
- `[1.00, 1.40)`: inactive;
- `[1.40, animmotion end)`: scale 0–1.

Controls outside the translation-curve range are clamped to that range and generate a warning. At the same timestamp, the last control encountered in annotation-track traversal order wins; avoid same-time controls because that ordering is easy to misunderstand.

### Explicit control and fail-closed behavior

The presence of any exact reserved `animwarp` or `animwarpend` token—valid or malformed—makes the entire clip explicitly controlled:

- implicit attack warping is disabled for the whole clip;
- the interval before the first valid `animwarp` is inactive;
- `animwarpend` remains inactive until a later valid `animwarp`;
- a valid explicit segment can warp even if the actor is not attacking;
- the INI setting `bEnableForAttackAnimations` does not disable valid explicit segments.

A malformed exact control does not add a segment marker, but it still suppresses default attack warping and produces a warning in `AnimationMotionRevolution.log`. This prevents a typo from unexpectedly falling back to implicit combat behavior.

These controls are malformed:

```text
animwarp -1 1
animwarp 1 0.5
animwarp 0
animwarp 0 1 181
animwarp 0 1 60 -1
animwarp 0 1 60 300 extra
animwarpend extra
```

## 6. Implicit warping for attack animations

If a clip has valid `animmotion`, contains no reserved warp controls, and default attack warping is enabled, AMR creates implicit combat segments. They apply only while the actor reports `IsAttacking()`.

The packaged defaults are equivalent to:

```text
0.000000 animwarp 0 1 60
```

The scale and angle actually come from the INI. Implicit warping has no maximum-distance gate.

AMR ends the current implicit segment and begins a new one at each of these events, searched across every annotation track:

- exact case-insensitive `HitFrame`, optionally followed by a dot payload such as `HitFrame.$payload`;
- any case-insensitive annotation text beginning with `Collision_Add`, such as `Collision_Add.Node(WEAPON)`.

Leading spaces/tabs are ignored for these two boundary-event checks. `preHitFrame`, `NPCHitFrame`, and `2_HitFrame` do not match. Because `Collision_Add` is currently a raw prefix test, a name such as `Collision_Additional` also matches; reserve that prefix for intended collision boundaries.

Example:

```text
0.000000 animmotion 0 0 0
0.700000 HitFrame
1.100000 Collision_Add.Node(WEAPON)
1.500000 animmotion 0 180 0
```

The implicit timeline is `[0, 0.7)`, `[0.7, 1.1)`, and `[1.1, 1.5)`. Each segment calculates a new scale so consecutive hits can realign against the current target. Boundaries at the same time, or within `0.0001` seconds, are merged.

## 7. Warp calculation and target rules

At normal segment entry, AMR samples the cumulative translation at the two exact boundaries:

```text
authoredVector = animmotion(segmentEnd) - animmotion(segmentStart)
authoredDistance = lengthXY(authoredVector)
desiredDistance = max(0, horizontalTargetDistance - fStopDistance)
requestedScale = desiredDistance / authoredDistance
finalScale = clamp(requestedScale, minimumScale, maximumScale)
```

Boundary translations are cached when the clip activates, so this calculation is constant-time. If warping first becomes valid after a segment has already begun—for example, after acquiring/changing a target, re-entering a distance gate, or entering the attack state—AMR uses the **remaining** cumulative vector from the current cursor to the same segment end.

Warping is considered applied only when all relevant conditions pass:

- the actor has a resolvable `currentCombatTarget`;
- the target is alive;
- actor and target are in the same cell;
- current horizontal distance does not exceed an explicit maximum distance;
- horizontal authored remainder is at least `fMinimumAuthoredDistance` and is nonzero;
- the angle from the authored world-space vector to the target vector does not exceed the limit;
- for an implicit segment only, default attack warping is enabled and the actor is attacking.

The angle gate compares **authored motion direction to target direction**, not actor facing to target direction. A target behind the actor may therefore warp a segment whose authored motion also moves backward. Conversely, AMR never rotates a forward segment to reach a target behind it.

Actor and target positions are compared horizontally by their reference positions; collision-capsule radii are not subtracted. Use `fStopDistance` to preserve a desired separation.

Once calculated, the scale is cached for the same target and segment. AMR does not continually chase a moving target by recomputing scale every frame. Target validity, same-cell status, maximum distance, and implicit attack state can deactivate the warp; returning to a valid state recalculates from the remaining motion. The angle is not rechecked while a cached scale remains active. A new segment always recalculates.

The segment distance is the magnitude of its **net displacement**, not the length traveled along a curved or reversing path. Every incremental X/Y displacement inside the segment receives the same scale. Add controls at direction changes when backward and forward phases should be treated separately. Large upper scales amplify all horizontal motion in the segment and may produce sliding or unnaturally fast movement.

## 8. Behavior graph variable

AMR publishes this Boolean graph variable for behavior authors:

```text
AMR_IsAnimationWarpingEnabled
```

Its exact runtime meaning is:

- `true`: at least one active AMR translation clip for that actor currently has motion warping **applied**;
- `false`: no active AMR translation clip currently has an applied warp.

“Applied” is stricter than “the clip contains `animwarp`.” A segment with no valid target, a failed gate, insufficient authored motion, an inactive `animwarpend`, a disabled/non-attacking implicit rule, or no `animmotion` reports false. A valid evaluation whose clamped result happens to be scale 1 still reports true.

AMR aggregates concurrently active tracked clips so a non-warping blended clip does not overwrite a warping clip's true value. It reasserts the aggregate during custom-translation sampling and clears/recomputes it when a tracked clip deactivates.

The distribution includes this Behavior Data Injector configuration:

```text
Data/SKSE/Plugins/BehaviorDataInjector/AnimationMotionRevolution_BDI.json
```

Equivalent JSON:

```json
[
  {
    "projectPath": "Actors",
    "type": "kBool",
    "name": "AMR_IsAnimationWarpingEnabled",
    "value": false
  }
]
```

`projectPath: "Actors"` asks Behavior Data Injector to add the false-initialized variable recursively to behavior projects under `Data/Meshes/Actors`. Install a compatible Behavior Data Injector (and the appropriate runtime support it requires). No Nemesis or Pandora generation is required for this JSON injection. Without the injected variable, AMR root motion and warping still run, but behavior graphs cannot reliably read this custom variable.

## 9. Edge protection

Edge protection is INI-driven; there is no annotation that directly enables it. It is evaluated after motion warping and can affect any horizontal direction—forward, backward, or lateral—when all of these are true:

- the clip has custom `animmotion` translation;
- `EdgeProtection.bEnableForAttackAnimations` is true;
- the actor reports `IsAttacking()`;
- the entire `animmotion` curve has no key with a nontrivial Z value;
- the current horizontal step is at least `fMinimumHorizontalDelta`.

Any nonzero Z key in the custom curve disables edge limiting for that whole clip, allowing upward or forward-upward jumps to retain their authored motion. AMR does not inspect vanilla translation for this test.

For an eligible step, AMR:

1. predicts the actor center after the current warped X/Y step;
2. obtains the horizontal footprint radius from the actor's Havok character-controller convex shape when available;
3. moves the probe from the predicted center to the controller boundary in the intended movement direction;
4. casts straight down in world Z using the actor's collision group and character-controller layer;
5. suppresses that frame's X/Y displacement if the ray hits no support.

Blocked displacement is consumed rather than accumulated, so stepping back onto valid ground does not cause a catch-up jump. Z remains unchanged. If a Havok world is unavailable, the check fails open and does not block motion. Any accepted ray hit counts as support; the system does not classify material, slope, or navmesh walkability.

## 10. Configuration defaults

`Data/SKSE/Plugins/AnimationMotionRevolution.ini` ships with:

| Section/key | Default | Meaning |
| --- | ---: | --- |
| `MotionWarping.bEnableForAttackAnimations` | `true` | Enable implicit combat segments; explicit valid segments ignore this switch. |
| `MotionWarping.fDefaultMinimumScale` | `0.0` | Implicit minimum X/Y scale. |
| `MotionWarping.fDefaultMaximumScale` | `1.0` | Implicit maximum X/Y scale. |
| `MotionWarping.fDefaultMaximumAngleDegrees` | `60.0` | Implicit direction gate. |
| `MotionWarping.fStopDistance` | `0.0` | Global desired horizontal separation, applied to explicit and implicit warp. |
| `MotionWarping.fMinimumAuthoredDistance` | `1.0` | Minimum remaining horizontal authored distance required to warp. |
| `EdgeProtection.bEnableForAttackAnimations` | `true` | Enable attack edge protection. |
| `EdgeProtection.fRaycastStartHeight` | `50.0` | Skyrim units above the predicted boundary where the ray starts. |
| `EdgeProtection.fRaycastDownwardRange` | `200.0` | Ray length downward from its elevated start. |
| `EdgeProtection.fMinimumHorizontalDelta` | `0.10` | Smallest frame step eligible for a probe. |
| `EdgeProtection.bDebugDraw` | `true` | Permit the optional TrueHUD ledge visualization. |

Increasing the downward range accepts support farther below the actor and therefore makes stepping/falling off an edge more likely. Making it too small can block movement over modest height changes.

## 11. Optional TrueHUD developer visualization

TrueHUD diagnostics exist only in builds compiled with `AMR_ENABLE_TRUEHUD_DEBUG`. When the macro is disabled, AMR does not compile or include the TrueHUD API integration. Both views begin hidden, and neither hotkey changes gameplay:

- **Page Up:** toggle motion-warp snapshots;
- **Page Down:** toggle edge-protection probes.

Warp visualization draws a one-second, world-anchored snapshot when an explicit segment activates, or when an implicit segment activates while attack warping is eligible. It can draw again when an already-running eligible segment first starts applying after a target, attack, angle, or distance condition becomes valid.

- yellow: full authored segment vector;
- cyan, raised 8 units: scaled segment prediction when a warp is applied;
- inactive intervals such as `animwarpend`: no warp snapshot.

The snapshot uses actor position and facing at draw time; it does not follow later turns. It represents the whole segment, not the shrinking remaining vector.

The edge view is refreshed for each eligible probe: yellow marks predicted-center to controller-boundary offset, green marks a supporting ray hit, and red marks a miss. Probe primitives last briefly so consecutive checks appear continuous.

## 12. Complete authored example

```text
# Timestamp  Annotation text
0.000000     animmotion 0 0 0
0.000000     animrotation 0
0.150000     animwarp 0 1.20 60 350
0.350000     animmotion 0 45 0
0.500000     animrotation 15
0.700000     animmotion 0 110 0
0.700000     animwarpend
1.000000     animmotion 0 135 0
1.000000     animrotation 0
```

- Translation and rotation are cumulative.
- `[0, 0.15)` uses custom motion without warping.
- `[0.15, 0.70)` may scale X/Y from 0% to 120% when target gates pass.
- `[0.70, 1.00)` uses custom motion without warping.
- Z and the authored rotation are never scaled by `animwarp`.
- Because this clip contains explicit warp control, it never receives implicit attack segmentation.

Warp-control times do not need to coincide with motion keys. AMR interpolates and caches the cumulative translation at the exact control time.

## 13. Author checklist and limitations

Before shipping an animation:

1. Confirm the behavior consumes animation-driven translation/rotation.
2. Put all `animmotion` and `animrotation` keys in one annotation track.
3. Use lowercase names, ASCII spaces, finite values, and unique timestamps.
4. Treat XYZ and yaw as cumulative values; begin at zero and end at the clip duration.
5. Add intermediate yaw keys for large turns.
6. Decide whether the clip should use implicit combat warping or explicit controls; any exact reserved control suppresses the implicit mode.
7. Place warp boundaries around phases whose net directions differ.
8. Remember that explicit warp still needs the actor's current combat target.
9. Install the bundled BDI JSON with Behavior Data Injector if behaviors consume `AMR_IsAnimationWarpingEnabled`.
10. Check `AnimationMotionRevolution.log` for duration mismatches, malformed warp controls, segment summaries, and scale diagnostics.

Important limitations:

- AMR does not warp vanilla-only root motion; warping requires custom `animmotion`.
- Warping scales but never rotates, predicts target motion, or continually rescales toward a moving target.
- Distance uses actor reference positions, not collision-surface separation.
- A large angle allowance can permit scaling motion that is lateral to—or even away from—the target.
- Reversing paths are evaluated by net segment displacement, so they require intentional segmentation.
- Edge protection is a downward physics-support test, not navmesh pathfinding.
- Motion/rotation keys split across tracks or duplicated at one timestamp are not reliably defined by the current parser.

For the original annotation convention, see the [Animation Motion Revolution mod page](https://www.nexusmods.com/skyrimspecialedition/mods/50258). For graph-variable injection format, see the [Behavior Data Injector configuration guide](https://github.com/max-su-2019/BehaviorDataInjector/blob/master/doc/How%20to%20create%20BDI%20config%20files.md).
