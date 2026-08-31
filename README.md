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

While an actor is attacking and has a current combat target, the authored
horizontal root-motion path is uniformly scaled so its travel distance matches
the target distance. Its authored direction is never rotated or steered. Motion
may be reduced to zero; extension beyond the authored distance is disabled.
Starting or ending the attack state mid-clip rebases the cumulative
path so enabling or disabling warping does not cause a position snap. An angle
gate limits scaling to animations already directed roughly toward the target.

Animations can override the attack-only default with timestamped annotations:

```text
animwarp <lowerScale> <upperScale> [maximumAngleDegrees] [maximumDistance]
```

The most recent `animwarp` at or before the current animation time controls the
allowed horizontal scale and enables warping even while the actor is not
attacking. Later annotations replace earlier ones. Before the first annotation,
or when none exist, AMR uses the INI-configured attack-animation defaults.
The optional angle defaults to 60 degrees and replaces the direction-angle limit
for that rule. The optional distance is a maximum current horizontal
actor-to-target distance; beyond it, the rule is inactive and authored motion
plays unchanged. Distance defaults to unlimited. Because arguments are
positional, an angle must be supplied before a distance.

The INI can disable default warping for attack animations without disabling
animations that explicitly opt in through an active `animwarp` annotation.
Default unannotated minimum/maximum scale and angle are independently configurable.

### TrueHUD debug visualization

Configure with the single CMake option `AMR_ENABLE_TRUEHUD_DEBUG`, enabled by
default. When disabled, the TrueHUD API and integration sources are omitted from
the plugin target and all initialization, input, and draw calls are compiled out.
When enabled, both debug views start hidden. Page Up toggles motion-warp
visualization, while Page Down independently toggles ledge-prevention
visualization. Neither hotkey changes gameplay behavior.

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
