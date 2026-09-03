# Animation Motion Revolution

This fork ports alexsylex's Animation Motion Revolution to CommonLibSSE-NG 6.8.0
and adds player root-motion safety and combat-target warping.

## Runtime support

- Skyrim SE 1.5.97
- Skyrim AE 1.6.x and current Address Library-supported AE runtimes
- VR is intentionally disabled because AMR's native hook sites have not been
  validated against the VR executable.

The CommonLibSSE-NG submodule is pinned to tag v6.8.0, commit
44dd911486bc43b05b55a23781c7e471eef86542.

## New features

### Downward ground probe

For each attacking-actor frame driven by an animmotion curve, AMR evaluates the next
horizontal root-motion step after warping. It casts a ray straight down on the
world Z axis at that predicted destination. If no support is hit, the horizontal
step is suppressed without accumulating a later catch-up jump. Vertical root
motion is left intact.

### Combat-target motion warping

While an actor is attacking and has a current combat target, authored
horizontal root motion is uniformly scaled so its travel distance matches the
target distance. Its authored direction is never rotated or steered. Motion may
be reduced to zero; extension beyond the authored distance is disabled by
default. An angle gate limits scaling to segments already directed roughly
toward the target.

Warping is segment based. A segment's authored motion is the cumulative
`animmotion` displacement at its end minus the cumulative displacement at its
start. Segment endpoints are sampled and cached once when the clip activates;
runtime scale calculation is constant time. If one game update crosses a
segment boundary, AMR applies the old scale up to the exact boundary and the new
scale after it, preserving the whole frame's motion. Normal activation uses the
complete segment displacement. If a target is acquired or a distance gate is
re-entered after the segment has begun, recalculation uses only the authored
motion still remaining to that same segment end.

Animations can override the attack-only default with timestamped annotations:

```text
animwarp <lowerScale> <upperScale> [maximumAngleDegrees] [maximumDistance]
animwarpend
```

An `animwarp` starts a segment at its timestamp. That segment ends at the next
`animwarp`, the next `animwarpend`, or the end of the `animmotion` curve.
`animwarpend` disables warping from its timestamp onward until a later
`animwarp`; it is synonymous with `animwarp 1 1`. Later controls at the same
timestamp replace earlier ones.

Any valid or malformed reserved warp-control annotation makes the clip
explicitly controlled and suppresses default attack warping for the entire
clip. Consequently, the interval before its first valid `animwarp` is
unwarped. This fail-closed behavior prevents a typo from unexpectedly enabling
the implicit attack rule. An explicit `animwarp` enables its segment even while
the actor is not attacking.

The optional angle defaults to 60 degrees and replaces the direction-angle limit
for that rule. The optional distance is a maximum current horizontal
actor-to-target distance; beyond it, the rule is inactive and authored motion
plays unchanged. Distance defaults to unlimited. Because arguments are
positional, an angle must be supplied before a distance.

When a clip contains no warp controls, INI-enabled attack warping starts with an
implicit `animwarp 0 1` (using the configured default limits). Every exact,
case-insensitive `HitFrame` event or event whose name begins with
`Collision_Add` ends that segment and starts a new default segment immediately.
Payloads such as `HitFrame.$payload` and `Collision_Add.Node(WEAPON)` are
supported. This recalculates the scale for each
successive hit instead of aiming the entire animation only once. The INI can
disable this default without disabling explicitly annotated segments. Default
minimum/maximum scale and angle are independently configurable.

### TrueHUD debug visualization

Configure with the single CMake option `AMR_ENABLE_TRUEHUD_DEBUG`, enabled by
default. When disabled, the TrueHUD API and integration sources are omitted from
the plugin target and all initialization, input, and draw calls are compiled out.
When enabled, both debug views start hidden. Page Up toggles motion-warp
visualization, while Page Down independently toggles ledge-prevention
visualization. Neither hotkey changes gameplay behavior. Motion-warp debug draws
a one-second snapshot when an explicit `animwarp` segment or generated default
combat segment activates. It also draws when an eligible segment first begins
applying after a late attack-state, target, angle, or distance-gate change.
Yellow is the segment's authored motion, and elevated cyan is its predicted
scaled motion when warping applies. Inactive segments, including `animwarpend`,
do not draw a snapshot. The target point and connector are intentionally not
drawn.

Settings live in
Data/SKSE/Plugins/AnimationMotionRevolution.ini.

## Build

Set VCPKG_ROOT to a valid vcpkg checkout, initialize submodules, and run:

    cmake --preset release-se-ae
    cmake --build --preset release-se-ae

The build uses the x64-windows-static-md triplet and keeps its output in the
sibling AnimationMotionRevolution-build directory.

## References and licensing

The original AMR source is MIT licensed. Targeting and orientation patterns were
compared with True Directional Movement, while raycast usage was checked against
SmoothCam and the current CommonLibSSE-NG bhkPickData API. No source was copied
from those projects.

CommonLibSSE-NG 6.8.0 is GPL-3.0-or-later with its documented modding and linking
exceptions. Anyone redistributing a linked binary must review and comply with
CommonLibSSE-NG's current COPYING and EXCEPTIONS.md terms.

Original Nexus page:
https://www.nexusmods.com/skyrimspecialedition/mods/50258
