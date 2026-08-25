# Shooting Stars Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a subtle, always-running shooting-star effect crossing the `MoonBg`/`BGSTars` backdrop behind Cooper, per `docs/superpowers/specs/2026-08-25-shooting-stars-design.md`.

**Architecture:** One Niagara System (`NS_ShootingStars`) with a single CPU sprite emitter, driven entirely through stock Niagara modules/dynamic-inputs (no scratch-pad HLSL, no new C++), using a procedural unlit/translucent streak material (`M_ShootingStarStreak`, no imported texture). Placed as a Niagara-system actor in front of the backdrop.

**Tech Stack:** Unreal Engine 5.8, Niagara (CPU simulation), Unreal Material Editor — all built and verified through the project's MCP editor toolset (`editor_toolset.toolsets.material.MaterialTools`, `NiagaraToolsets.NiagaraToolset_System`, `NiagaraToolsets.NiagaraToolset_Assets`, `editor_toolset.toolsets.scene.SceneTools`, `EditorToolset.EditorAppToolset.CaptureViewport`).

## Global Constraints

- Trigger: always running in every avatar/conversation state — never gated on `OnAvatarStateChange` or any other signal.
- Density: one streak alive at a time; next spawn randomized 8–15s after the previous cycle starts (see Task 5 note on the loop-duration simplification).
- Per-streak lifetime: random 0.7–1.4s.
- Direction: one base diagonal (upper-left → lower-right across the backdrop face) with ±10–15° per-streak random jitter.
- Color: uniform-random pick from exactly 4 palette entries — blue-white `(0.75,0.85,1.0)`, gold `(1.0,0.85,0.5)`, pale purple `(0.8,0.7,1.0)`, pale green `(0.75,1.0,0.8)` — not a continuous random hue.
- Thickness: randomized per streak, independent of color.
- Fade in fast at spawn, fade out at end of life.
- No new imported textures/assets — streak visual is procedural.
- Backdrop reference transform (do not modify): `MoonBg` actor at Location `(-454, -3816, 1267)`, Rotation `(0,0,0)`, Scale `0.3805` uniform, mesh `/Game/Geo/BgStars/MoonBg`, material `/Game/Geo/BgStars/BGSTars`.

## Derived world-space geometry (for Task 3)

Computed from the backdrop's transform and its local mesh bounds (`X: ±16131.32`, `Y: -1106.77..6274.76`, `Z: ±8340.78`, scale `0.3805`, rotation identity so local axes = world axes):

- Visible face world X range: `-6592 .. 5684`
- Visible face world Z range: `-1907 .. 4441`
- Backdrop's nearest-to-camera depth: world `Y ≈ -1427`

**Simplification note (deviation from spec wording, documented here for the implementer):** the spec describes speed as "distance to cross, divided by lifetime." Deriving that exactly requires chaining a Niagara "Particle Attribute Reader" against `Particles.Lifetime` inside a custom dynamic-input tree — extra risk for a purely cosmetic, ambient effect. Task 3 instead uses two *independently* randomized ranges (lifetime 0.7–1.4s, speed 65–130 m/s) chosen so typical travel distance (≈4550–18200cm) overlaps the backdrop's actual diagonal extent (≈9000–13800cm). Visually indistinguishable for a background effect; if playtesting in Task 6 shows streaks regularly under- or over-shooting the visible face, narrow the speed range then — don't rebuild the attribute-reader chain unless the simple version visibly fails.

---

### Task 1: Procedural streak material

**Files:**
- Create: `/Game/Geo/BgStars/M_ShootingStarStreak` (Material asset)

**Interfaces:**
- Produces: `/Game/Geo/BgStars/M_ShootingStarStreak` — an Unlit, Translucent material with usage flag "Used with Niagara Sprites" enabled, that reads per-particle color via a `ParticleColor` material expression and shapes a gradient trail from the sprite's own UVs. Consumed by Task 2's Sprite renderer.

- [ ] **Step 1: Create the material asset**

Use `editor_toolset.toolsets.material.MaterialTools.create_material` (or the equivalent create-asset call used earlier this session for `MoonMat`) to create `/Game/Geo/BgStars/M_ShootingStarStreak`.

- [ ] **Step 2: Set base material properties**

Set `BlendMode = BLEND_Translucent`, `ShadingModel = MSM_Unlit`. Then check the material's property schema (`get_..._schema` equivalent, or `list_properties`) for the Niagara-sprite usage flag (commonly `bUsedWithNiagaraSprites`) and enable it — without this flag the material won't be assignable to a Sprite renderer.

- [ ] **Step 3: Build the gradient-shape graph**

Add expressions and wire them:
- `TexCoord0` (UV) → `ComponentMask` (R only) → `OneMinus` → `Power` (exponent ~2.0) — length-wise fade, bright at the leading edge, transparent at the tail. *(If the resulting streak looks backwards after Task 6's viewport check — bright tail instead of bright head — flip this to mask the U channel the other way; Niagara's velocity-aligned sprite UV convention should be confirmed visually, not assumed.)*
- `TexCoord0` (UV) → `ComponentMask` (G only) → `Subtract` (0.5) → `Abs` → `OneMinus` → `Power` (exponent ~1.5) — width-wise soft rounded falloff.
- `Multiply` the two gradients together → this is the shape mask.

- [ ] **Step 4: Wire particle color and connect outputs**

Add a `ParticleColor` expression (stock Niagara/Cascade material node exposing per-particle `Particles.Color` as RGB + A).
- `ParticleColor.RGB` → `Multiply` by a `Constant` (e.g. `4.0`, for an unlit glow bright enough to read against the dark backdrop) → **Emissive Color**.
- `Multiply` the Step 3 shape mask by `ParticleColor.A` (carries the per-particle fade-in/out set in Task 4) → **Opacity**.

- [ ] **Step 5: Recompile and save**

Recompile the material, confirm no errors, then `save_assets(asset_paths: [])`.

---

### Task 2: Niagara System skeleton

**Files:**
- Create: `/Game/Geo/BgStars/NS_ShootingStars` (Niagara System asset)

**Interfaces:**
- Consumes: `/Game/Geo/BgStars/M_ShootingStarStreak` (Task 1).
- Produces: `/Game/Geo/BgStars/NS_ShootingStars`, containing one emitter (name it `ShootingStar`), CPU-simulated, `Local Space = false` (all positions/velocities computed in Task 3 are world-space), `Max Particles = 2`. Sprite renderer referencing `M_ShootingStarStreak`, screen alignment = velocity-aligned. Later tasks add modules onto this emitter's stacks.

- [ ] **Step 1: Create the system and emitter**

`NiagaraToolsets.NiagaraToolset_System.CreateNiagaraSystem` at `/Game/Geo/BgStars/NS_ShootingStars`. `AddEmitter` to it, named `ShootingStar`, CPU sim target.

- [ ] **Step 2: Set emitter properties**

Call `GetEmitterSchema` to find the exact property names for local-space and max-particle-count, then `SetEmitterData` to set `Local Space = false` and `Max Particles = 2`.

- [ ] **Step 3: Add the Sprite renderer**

`AddRenderer` (Sprite renderer class) to the `ShootingStar` emitter. `SetRendererData` (checking `GetRendererSchema` first for exact property names) to set:
- Material = `/Game/Geo/BgStars/M_ShootingStarStreak`
- Screen/Sprite Alignment = Velocity Aligned

- [ ] **Step 4: Verify topology**

Call `GetEmitterTopology` / `GetEmitterSummary` and confirm: one emitter, CPU sim, one Sprite renderer pointing at `M_ShootingStarStreak`. Save (`save_assets`).

---

### Task 3: Per-particle spawn attributes (position, direction+jitter, speed, lifetime, width)

**Files:**
- Modify: `/Game/Geo/BgStars/NS_ShootingStars` (Particle Spawn script stack on the `ShootingStar` emitter)

**Interfaces:**
- Consumes: emitter from Task 2.
- Produces: on every spawned particle — `Particles.Position` (world-space, inside the backdrop's visible band), `Particles.Velocity` (jittered-diagonal direction × random speed), `Particles.Lifetime` (0.7–1.4s), `Particles.SpriteSize` (randomized thin/thick). Task 4 reads/writes `Particles.Color` on top of this same stack.

- [ ] **Step 1: Set lifetime on the built-in Initialize Particle module**

The `Initialize Particle` module is present by default in the Particle Spawn stack. Use `GetModuleSchema` on it to find the `Lifetime` input, then `SetStackInputData` to bind it to a `Random Range Float` dynamic input with `Min = 0.7`, `Max = 1.4`.

- [ ] **Step 2: Add a Set Parameters module for position**

`AddSetParametersModule` on the Particle Spawn stack (after Initialize Particle). `AddSetParameterEntry` for `Particles.Position` (Vector), value built from:
- `X`: `Random Range Float`, `Min = -6592`, `Max = -1500`
- `Y`: constant `-1350`
- `Z`: `Random Range Float`, `Min = 2200`, `Max = 4300`

- [ ] **Step 3: Add jittered-direction + speed → velocity**

In the same Set Parameters module, add `Particles.Velocity` (Vector). Build the value as: a jitter angle `Random Range Float` (`Min = -15`, `Max = 15`, degrees) added to the fixed base angle `-45`, converted to a unit vector `(cos, 0, sin)` in world X/Z, multiplied by a `Random Range Float` speed (`Min = 6500`, `Max = 13000`, cm/s). Use `GetAvailableDynamicInputs` (filtered to Vector/Float as appropriate) to find the exact stock trig/multiply dynamic-input assets available in this engine version before wiring — don't guess asset paths.

- [ ] **Step 4: Add randomized sprite width**

Add `Particles.SpriteSize` (Vector2D) to the same module: `X` (width) = `Random Range Float`, `Min = 8`, `Max = 40`; `Y` (length) can stay a fixed base (e.g. `60`) since velocity-aligned stretching handles the visual length — width is the "thin vs thick" axis per the spec.

- [ ] **Step 5: Verify**

`GetScriptStackInputValues` on the Particle Spawn stack, confirm all four entries (`Lifetime`, `Position`, `Velocity`, `SpriteSize`) resolve to the expected dynamic-input ranges with no stack errors (`GetStackIssues` returns empty/no errors). Save.

---

### Task 4: Per-particle color palette + lifetime fade

**Files:**
- Modify: `/Game/Geo/BgStars/NS_ShootingStars` (Particle Spawn and Particle Update stacks on `ShootingStar`)

**Interfaces:**
- Consumes: the Set Parameters module from Task 3 (Particle Spawn stack).
- Produces: `Particles.Color` set once at spawn (RGB = one of the 4 palette entries) and faded over the particle's life (A ramps 0→1→0), read directly by `M_ShootingStarStreak`'s `ParticleColor` node from Task 1.

- [ ] **Step 1: Add the discrete color pick at spawn**

In the Task 3 Set Parameters module (Particle Spawn stack), add `Particles.Color` (LinearColor). Use `GetAvailableDynamicInputs` filtered to `LinearColor`/Color type to find the stock "Select" (N-way branch) dynamic input, driven by a `Random Range Integer` (`Min = 0`, `Max = 3`). Wire its 4 option slots to the palette constants:
- `0` → `(0.75, 0.85, 1.0, 1.0)` (blue-white)
- `1` → `(1.0, 0.85, 0.5, 1.0)` (gold)
- `2` → `(0.8, 0.7, 1.0, 1.0)` (pale purple)
- `3` → `(0.75, 1.0, 0.8, 1.0)` (pale green)

If no stock "Select" dynamic input is found for this type in `GetAvailableDynamicInputs`, search `NiagaraToolsets.NiagaraToolset_Assets.FindNiagaraScripts` for `"Select"` before falling back to anything else. If genuinely unavailable in this engine version, fall back to nesting nested `If`/branch dynamic inputs (a near-universal stock building block) comparing the `Random Range Integer` result against `0`, `1`, `2` in turn, each branch outputting one palette constant. Never substitute a continuous-random color — that violates the spec's discrete-palette requirement.

- [ ] **Step 2: Add lifetime-based fade in the Particle Update stack**

Add a `Scale Color` (or equivalently-named stock fade module — confirm via `FindNiagaraScripts` search for `"Scale Color"`/`"Color"` if the exact name differs) module to the Particle Update stack, driven by `Particles.NormalizedAge`, shaped to ramp alpha `0 → 1` over the first ~15% of life and `1 → 0` over the last ~25% of life (fast fade-in, slightly longer fade-out, per spec). This multiplies into the `Particles.Color.A` set in Step 1, which `M_ShootingStarStreak`'s Opacity math (Task 1, Step 4) already reads.

- [ ] **Step 3: Verify**

`GetScriptStackInputValues` on both stacks; confirm no stack errors. Save.

---

### Task 5: Recurring randomized spawn timing

**Files:**
- Modify: `/Game/Geo/BgStars/NS_ShootingStars` (Emitter Update and Emitter Spawn stacks on `ShootingStar`)

**Interfaces:**
- Consumes: emitter from Task 2.
- Produces: the emitter loops indefinitely, each loop spawning exactly one particle at loop-start, loop length randomized 8–15s per cycle — this is what gives "one streak alive at a time, next one 8–15s later."

- [ ] **Step 1: Configure the Emitter State module for randomized looping**

The `Emitter State` module is present by default in the Emitter Update stack. `GetModuleSchema` on it to find `Loop Behavior` and `Loop Duration`. `SetStackInputData` to set `Loop Behavior = Multiple` (or `Infinite` if that's the exact enum name in this engine version — confirm via the schema, don't guess) with `Loop Duration` bound to a `Random Range Float` dynamic input, `Min = 8.0`, `Max = 15.0`. Set `Loop Count = 0` if that's how this engine version expresses "infinite" (confirm via schema).

**Note on the 8–15s window:** the spec phrases this as "next streak starts 8–15s after the previous one ends." `Loop Duration` here measures spawn-to-spawn (start-to-start), not end-to-start. Since particle lifetime (0.7–1.4s) is small relative to the 8–15s loop, the two interpretations differ by at most ~1.4s out of a ~10s+ gap — a small enough difference to be visually indistinguishable, so this is treated as satisfying the requirement as written. Don't add extra logic to compensate for the difference.

- [ ] **Step 2: Add a one-shot burst per loop**

Add a `Spawn Burst Instantaneous` module to the Emitter Spawn stack (`AddModule`, discover the exact module asset via `FindNiagaraScripts` search for `"Spawn Burst Instantaneous"` first). Set `Spawn Count = 1`, `Spawn Time = 0` (relative to loop start), `Spawn Probability = 1.0`.

- [ ] **Step 3: Verify no continuous spawning is left enabled**

Confirm via `GetScriptStackTopology` on the Emitter Spawn stack that no `Spawn Rate` module is present/enabled (it should never have been added, but this is the point to check) — otherwise particles would spawn continuously in addition to the burst. Save.

---

### Task 6: Place in the level and verify

**Files:**
- Modify: `/Game/Lvl_Metahuman.umap` (adds one new actor)

**Interfaces:**
- Consumes: `/Game/Geo/BgStars/NS_ShootingStars` (Tasks 2–5, fully configured).
- Produces: a placed, saved, visually-verified actor in the level. This is the final deliverable — no later task depends on it.

- [ ] **Step 1: Place the system in the level**

`editor_toolset.toolsets.scene.SceneTools.add_to_scene_from_asset` with `asset_path = "/Game/Geo/BgStars/NS_ShootingStars"`, `name = "ShootingStars"`, `xform` = identity (Location `0,0,0`, Rotation `0,0,0`, Scale `1,1,1` — all particle positions/velocities from Task 3 are already absolute world-space, per Task 2's `Local Space = false`).

- [ ] **Step 2: Confirm it's alive in-editor**

Use `EditorToolset.EditorAppToolset.CaptureViewport` (framed on the backdrop, matching the earlier moon-texture verification approach this session) at two or three points a few seconds apart to confirm at least one streak is visible crossing the backdrop, with a visible gradient trail (not a hard-edged box) and a plausible palette color.

- [ ] **Step 3: Confirm always-on behavior**

Cross-check: nothing in this system references `OnAvatarStateChange`, `BP_KioskController`, or `WBP_PushToTalk` (grep the level/Blueprint graphs touched this session, or simply note by construction that Tasks 1–5 never touched those assets) — the effect runs unconditionally once placed, satisfying the "always running" requirement without needing a live conversation test.

- [ ] **Step 4: Save and commit**

`save_assets(asset_paths: [])`. Then:

```bash
cd "/Users/muhammad/Documents/Unreal Projects/Astronaut Final With Background"
git add Content/Geo/BgStars/ Content/Lvl_Metahuman.umap
git commit -m "$(cat <<'EOF'
Add shooting stars effect over the moon-surface backdrop

Co-Authored-By: Claude Sonnet 5 <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Hand off to the user**

Report back in-editor (screenshot or description) for the user's own live look, per the spec's testing plan — this plan's job is done once Steps 1–4 are complete and verified.
