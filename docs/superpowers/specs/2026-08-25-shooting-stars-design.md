# Shooting stars over the moon-surface backdrop

## Problem

The kiosk background behind Cooper is the `MoonBg` static mesh (`/Game/Geo/BgStars/MoonBg`) with the `BGSTars` material (`/Game/Geo/BgStars/BGSTars`) applied as an override on its `Material_001` slot, placed at `Location (-454, -3816, 1267)`, `Rotation (0,0,0)`, `Scale 0.3805` uniform. It's a static starfield with no motion. The ask: add occasional shooting stars crossing it, so the background feels alive without pulling focus from Cooper.

## Non-goals

- No changes to `MoonBg`/`BGSTars` themselves, or to their transform.
- No changes to avatar state, Blueprint event graphs, or the companion-service protocol — this is a self-contained ambient background effect with no gameplay/state dependency.
- Not tied to conversation state (see below) — always-on, so no wiring into `OnAvatarStateChange` or any other companion-service signal.
- No new imported textures/assets beyond what's created for this feature — the streak visual is built procedurally in a material graph.

## Behavior contract

Confirmed with the user during brainstorming; these are hard requirements, not starting points:

- **Trigger:** always running, regardless of avatar/conversation state (idle, listening, thinking, talking) — never paused or gated.
- **Density:** subtle. One streak alive at a time; next streak's start is randomized in the **8–15s** range after the previous one ends (not a fixed cadence).
- **Per-streak lifetime:** randomized in the **0.7–1.4s** range (how long one streak takes to cross), independent per streak.
- **Direction:** all streaks share one base diagonal travel direction (upper-left → lower-right across the backdrop's visible face), with a per-streak random angle jitter of roughly **±10–15°** so it reads as a natural meteor shower rather than a single repeating animation.
- **Color:** picked per-streak at random from a fixed palette of real meteor colors — not a full random hue wheel:
  - Blue-white (bright, e.g. RGB ~0.75/0.85/1.0)
  - Gold (RGB ~1.0/0.85/0.5)
  - Pale purple (RGB ~0.8/0.7/1.0)
  - Pale green (RGB ~0.75/1.0/0.8)
- **Thickness:** randomized per-streak, independent of color — thin↔thick range on the trail's width, tapering along its length either way.
- **Fade:** fast fade-in at spawn, fade-out at end of life — no hard pop on/off.

## Architecture

**Niagara System**, `/Game/Geo/BgStars/NS_ShootingStars`, containing one CPU sprite emitter. Niagara is the right tool here specifically because the requirement is per-instance randomized color/width/timing/direction — a Blueprint-timer-and-spawned-actor approach would need to hand-roll all of that bookkeeping, and a material-only trick can't give discrete per-streak color/width variation at all.

- **Spawning:** particle spawn is gated to produce at most one live particle at a time, with the gap before the next spawn randomized to 8–15s. Implemented with a spawn-control module (Scratch Pad or equivalent) rather than a flat continuous Spawn Rate, since a constant-rate/Poisson spawn can't guarantee the "one at a time, 8–15s gap" contract. `Max Particles` capped at 2 as a hard safety backstop.
- **Per-particle initialization** (on spawn): 
  - Lifetime = random 0.7–1.4s.
  - Start position = random point along the near edge of the backdrop's visible bounds (biased to the upper-left, matching the travel direction so streaks enter from off-frame rather than mid-frame).
  - Direction = base diagonal vector rotated by a random ±10–15° jitter.
  - Speed = distance to cross the backdrop's visible area, divided by that particle's own lifetime — ties speed to the randomized lifetime so short-lived streaks still visibly cross rather than crawling or over-shooting.
  - Color = uniform-random pick from the 4-entry palette above.
  - Width = random thin↔thick scalar, stored as a per-particle attribute feeding the material.
- **Renderer:** Sprite renderer, screen alignment set to velocity-aligned so each sprite stretches along its direction of travel, giving the streak shape without needing a Ribbon/trail history buffer.
- **Material:** `/Game/Geo/BgStars/M_ShootingStarStreak`, unlit translucent, built procedurally (no imported texture) — a soft gradient along the sprite's local U axis (opaque/bright at the leading edge, fading to fully transparent at the trailing edge) combined with a soft falloff across the V axis for a rounded rather than hard-edged trail. Tinted by the per-particle color attribute; opacity additionally modulated by a fade-in/fade-out curve over the particle's normalized age so it never pops.

**Placement:** a Niagara Component on an actor positioned in front of `MoonBg`, sized/oriented to roughly cover its visible face so streaks read as part of the star field rather than floating in front of it.

**Performance:** CPU-simulated, effectively one sprite alive at a time — negligible relative to the scene. Consistent with this project's existing Scalability settings, which already run particle/FX quality low (`fx.Niagara.QualityLevel:0`, `r.ParticleLightQuality:0`) for the kiosk's target hardware; nothing here needs GPU simulation or lights.

## Testing

No automated test harness for visual VFX in this project (consistent with prior sessions' convention — manual PIE/viewport verification). Plan:

1. Build the system, place the component, save.
2. Self-verify via `EditorAppToolset.CaptureViewport` across a couple of minutes of real time (or fast-forwarded PIE) to confirm: streaks actually appear at roughly the expected cadence, direction/jitter looks like a meteor shower and not a repeating loop, all 4 palette colors show up over several streaks, thickness visibly varies, and fade in/out has no hard pop.
3. Confirm it runs identically regardless of avatar state — trigger a conversation mid-effect and confirm no interruption/pause.
4. Hand off to the user for a live look in the editor before considering it done.
