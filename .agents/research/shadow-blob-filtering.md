# The floor shadow's blob: smooth filtering tried, and reverted

Question: a floor shadow vibrated with small camera turns, and the ground's texture showed through
it in patches — is that the blob's nearest-filtered texels? Tried 2026-11-14 against the fork on
upstream 1.0.3. Reverted the same day.

## What was tried

`FieldPolygonFlags::Shadow` (128) on both a character's and a gear's floor shadow, with
`FieldScene.usf` sampling those polygons `FILTER_BILINEAR` whatever the sprite filter asked for.

## What it did

- It did **not** stop either symptom.
- It visibly changed the look: the blob's stepped, pixel-art edge became a smooth falloff. The owner
  wants the stepped shape kept (it is the original's shadow), so the change was reverted.

## What that rules out, and what is left

The texels' filter is not the cause. Two candidates remain:

- **The shadow's depth test against the ground.** The port draws shadows last against the depth
  buffer, testing each one at a height above its floor so a ledge or a wall hides it. The original
  draws the shadow in the ordering table and never clips it against the ground. Where the ground under
  the 48-unit quad is uneven, or the interpolated test depth lands close to the ground's, parts of the
  shadow are hidden and the comparison flips as the camera turns — which would read as the ground
  showing through and as a shimmer.
- **The blending of the shadow's own soft blob.** It is a semi-transparent, subtractive blob: the
  ground's texture does show through its lighter texels by design, and at a modern resolution its
  16 x 16 gradient is magnified into large patches.

## How to tell them apart

Look at the same shadow, same map, in `D:\Games\Weltall-1.0.1-Windows` — the untouched released build,
kept as the reference. If it shimmers and shows the ground through there too, both are the port's own
look and behaviour rather than something this fork changed. If only the fork does it, the change that
brought it is in the fork's field rendering and should be bisected.
