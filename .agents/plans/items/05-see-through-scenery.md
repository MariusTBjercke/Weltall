# See-Through Scenery Implementation Plan

> **Done, and changed in play.** This is the plan as first written. What was built went further after the owner
> played each step: the hole became a round bubble of three sizes, only models that hide the party are seen
> through, gear models and fades were added, and several free-camera drawing faults were fixed on the way.
> `.agents/validation/m02-see-through.md` records what was built and verified.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On the field, scenery that stands between the camera and a party member (trees, rocks, walls,
overhangs) is drawn see-through about that member, the way CRPGs cut a hole in what hides the party.

**Architecture:** The core marks each party member's sprite outside cutscenes (`FFieldCharacterView::bSeenThrough`).
The renderer turns each marked sprite into a vertical capsule from its feet to its head (`FFieldSeeThroughHole`),
kept in the space of the identity object row so the player's free camera moves it with the sprites. The field-scene
pixel shader then discards scenery pixels that lie in front of a capsule and inside its outline on screen, with a 4 x 4
ordered dither for a soft edge. The math lives once in `WeltallCore` (tested with Catch2) and is mirrored in
`FieldScene.usf` (tested with an editor rendering test). No emulation behaviour changes.

**Tech Stack:** C++20 (`WeltallCore`, no Unreal), UE 5.8 RDG + HLSL (`Source/Weltall`, `Shaders/Private/FieldScene.usf`),
Catch2 (`Tests/`), Unreal automation tests (`Source/WeltallEditor/Private/Tests`).

**Spec:** this file. Design discussion: chat of 2026-11-07. In short:
- A dithered cut-out ("see-through bubble") rather than fading whole models. It does not depend on how a map splits
  its scenery into models, and dithering avoids the sorting trouble real transparency has against the depth buffer.
- Only scenery **in front of** the member and **above its feet** on screen is cut, so the floor and walls behind stay.
- Off in cutscenes: their framing is deliberate.
- An Options row, `See-through scenery`, Off / On, **default On** (the owner asked for it; confirm with the owner
  before committing, and flip the default to Off if they prefer the original look to come first).

## Global Constraints

- English in code, comments, docs and commit messages; upstream's commit style, with no `feat:` prefixes (`.agents/conventions.md`).
- `WeltallCore` builds with no exceptions, no RTTI, `/W4 /WX`.
- Gameplay semantics come from the original: nothing under `Source/WeltallCore/Private/Field/Event*`, the GTE, or the
  oracle-tested paths changes. Only the drawing and the option layer change.
- Keep changes small and in the files that already own each concern, so `git merge upstream/main` stays boring.
- Never commit game data or logs.
- `Scripts/package.py` builds `git archive HEAD`: commit before packaging.
- Player-visible text: plain, no exclamation marks; Options descriptions are three lines of at most 30 characters.

## Preconditions (check before Task 1)

- [ ] The other agent's work has landed: `git log --oneline -5` on `main` shows item 04 (message boxes) finished, and
      the `TEMPORARY: report what the field's drawing leaves out` commit (`0e6aa80`) has been reverted or replaced.
      Item 04 touches `FieldRenderOptions.h`, `OptionsText.h`, `MenuSettingsPages.cpp`, `PlayerSettings.cpp`,
      `FieldView.cpp` and the menu test; this plan touches the same files. **Do not start while that work is open.**
- [ ] `git status --short` is clean. Create the branch: `git switch -c see-through-scenery`.
- [ ] Re-read the line numbers quoted below against the current files. They were taken at `def21c9`.

## File Structure

| File | Change | Responsibility |
|---|---|---|
| `Source/WeltallCore/Public/Field/FieldRenderOptions.h` | modify | `EFieldSeeThrough`, `FFieldRenderOptions::SeeThrough` |
| `Source/WeltallCore/Public/Field/FieldSeeThrough.h` | create | hole type, constants, `IsPartyActor`, `SeeThroughHoleOf`, `SeeThroughShare` |
| `Source/WeltallCore/Private/Field/FieldSeeThrough.cpp` | create | their bodies |
| `Source/WeltallCore/Public/Field/FieldFrame.h` | modify | `FFieldCharacterView::bSeenThrough` |
| `Source/WeltallCore/Private/Field/FieldPresenter.cpp` | modify | set `bSeenThrough` in `DrawSprite` |
| `Tests/WeltallCore/Field/FieldSeeThroughTests.cpp` | create | Catch2 tests of the above (picked up by the CMake glob) |
| `Source/Weltall/Private/Rendering/FieldDrawBuilder.h` | modify | `FieldPolygonFlags::SeeThrough`, `AddFieldSeeThroughHoles` |
| `Source/Weltall/Private/Rendering/FieldSceneMesh.cpp` | modify | flag the map's scenery polygons; reset the holes in `BuildFieldSceneDraw` |
| `Source/Weltall/Public/Rendering/FieldSceneRendering.h` | modify | `FFieldSceneDraw::SeeThroughHoles`, `SeeThroughObject` |
| `Source/Weltall/Private/Rendering/FieldFrameGeometry.cpp` | modify | `AddFieldSeeThroughHoles` |
| `Source/Weltall/Private/Rendering/FieldFrameDraw.cpp` | modify | call it, reset the holes |
| `Source/Weltall/Private/Rendering/FieldSceneShaders.h` | modify | pixel shader parameters |
| `Source/Weltall/Private/Rendering/FieldSceneRendering.cpp` | modify | place the holes in the shown view, bind them |
| `Shaders/Private/FieldScene.usf` | modify | view position to the pixel shader, the discard |
| `Source/WeltallEditor/Private/Tests/FieldFrameRenderingTest.cpp` | modify | rendering test |
| `Source/WeltallCore/Public/Menu/OptionsText.h` | modify | the row's text |
| `Source/WeltallCore/Private/Menu/MenuSettingsPages.cpp` | modify | the row |
| `Source/WeltallCore/Private/Game/PlayerSettings.cpp` | modify | `field.see_through` |
| `Tests/WeltallCore/Menu/MenuSettingsPagesTests.cpp`, `Tests/WeltallCore/Game/PlayerSettingsTests.cpp` | modify | tests |
| `README.md` | modify | one bullet |
| `.agents/plans/progress.json`, `.agents/validation/m02-see-through.md` | modify / create | tracking and evidence |

Known gaps, out of scope (record them in the validation note):
- **Gears drawn as 3D models** (Options, Gears, default "Outside cutscenes") return before a character view is made, so
  they get no hole. A later item can build holes from `FFieldGearsFrame` bounds.
- **Scenes staged before a painted picture** (`FFieldPictureStretch::bStaged`) get no holes: the stretch moves the
  sprites by anchors the identity row does not follow.
- The world map and battles are untouched.
- No silhouette or "x-ray" outline of the hidden member. It would be a separate option.

---

### Task 1: The core: option, party marking and the hole's math

**Files:**
- Modify: `Source/WeltallCore/Public/Field/FieldRenderOptions.h` (enum next to `EMessageBoxBackground`, member at the end of `FFieldRenderOptions`)
- Create: `Source/WeltallCore/Public/Field/FieldSeeThrough.h`
- Create: `Source/WeltallCore/Private/Field/FieldSeeThrough.cpp`
- Modify: `Source/WeltallCore/Public/Field/FieldFrame.h:26-38` (`FFieldCharacterView`)
- Modify: `Source/WeltallCore/Private/Field/FieldPresenter.cpp:597-665` (`DrawSprite`)
- Test: `Tests/WeltallCore/Field/FieldSeeThroughTests.cpp`

**Interfaces:**
- Produces:
  - `enum class Weltall::EFieldSeeThrough : uint8 { Off, On };` and `FFieldRenderOptions::SeeThrough` (default `On`)
  - `struct Weltall::FFieldSeeThroughHole { std::array<float, 3> Feet; std::array<float, 3> Head; float HalfWidth; };`
  - `inline constexpr std::size_t Weltall::FieldSeeThroughHoles = 6;`
  - `namespace Weltall::FieldSeeThrough { DepthMargin, DepthFade, Widen, Edge, FeetFade, HeadRoom }` (float constants)
  - `bool Weltall::IsPartyActor(const FEventContext&, int32 Actor)`
  - `FFieldSeeThroughHole Weltall::SeeThroughHoleOf(std::span<const std::array<float, 3>> Corners)`
  - `float Weltall::SeeThroughShare(const FFieldSeeThroughHole&, const std::array<float, 3>& View)`
  - `bool FFieldCharacterView::bSeenThrough`

- [ ] **Step 1: Write the failing tests**

`Tests/WeltallCore/Field/FieldSeeThroughTests.cpp`:

```cpp
#include "Field/EventContext.h"
#include "Field/FieldSeeThrough.h"

#include <catch_amalgamated.hpp>

#include <array>

using namespace Weltall;

namespace
{
	/** A sprite 16 view units wide and 32 tall, its feet at the eye's height, 1000 ahead (y runs down). */
	FFieldSeeThroughHole StandingSprite()
	{
		const std::array<std::array<float, 3>, 4> Corners{{{-8.0f, -32.0f, 1000.0f}, {8.0f, -32.0f, 1000.0f}, {-8.0f, 0.0f, 1000.0f}, {8.0f, 0.0f, 1000.0f}}};
		return SeeThroughHoleOf(Corners);
	}
}

TEST_CASE("Only the actors in the party's filled slots are seen through", "[Field]")
{
	FEventContext Context;
	Context.PartyCharacters = {0, 2, FEventContext::NoActor};
	Context.PartyActors = {4, 7, 9};
	CHECK(IsPartyActor(Context, 4));
	CHECK(IsPartyActor(Context, 7));
	// The third slot is empty: its actor left over from a member gone does not count.
	CHECK_FALSE(IsPartyActor(Context, 9));
	CHECK_FALSE(IsPartyActor(Context, 5));
}

TEST_CASE("A sprite's hole runs from its feet to its head, as wide as its quads", "[Field]")
{
	const FFieldSeeThroughHole Hole = StandingSprite();
	CHECK(Hole.Feet == std::array<float, 3>{0.0f, 0.0f, 1000.0f});
	CHECK(Hole.Head == std::array<float, 3>{0.0f, -32.0f, 1000.0f});
	CHECK(Hole.HalfWidth == 8.0f);
	CHECK(SeeThroughHoleOf({}).HalfWidth == 0.0f);
}

TEST_CASE("The scenery is seen through only in front of the sprite, about it and above its feet", "[Field]")
{
	const FFieldSeeThroughHole Hole = StandingSprite();
	// Half way up, well in front: all of it.
	CHECK(SeeThroughShare(Hole, {0.0f, -8.0f, 500.0f}) == Catch::Approx(1.0f));
	// Behind the sprite, or in front by less than the margin (a wall it stands against): none.
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 1500.0f}) == 0.0f);
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 980.0f}) == 0.0f);
	// Half way through the depth fade: half.
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 936.0f}) == Catch::Approx(0.5f).margin(0.01));
	// Far to its side, or below its feet on the screen (the floor in front of it): none.
	CHECK(SeeThroughShare(Hole, {500.0f, -16.0f, 500.0f}) == 0.0f);
	CHECK(SeeThroughShare(Hole, {0.0f, 8.0f, 500.0f}) == 0.0f);
	// At nine tenths of its widened half width, level with its head: the soft edge.
	CHECK(SeeThroughShare(Hole, {6.3f, -16.0f, 500.0f}) == Catch::Approx(0.1f / FieldSeeThrough::Edge).margin(0.01));
	// Behind the eye: none.
	CHECK(SeeThroughShare(Hole, {0.0f, -8.0f, -10.0f}) == 0.0f);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run (from `Tests/`): `cmake --build --preset windows`
Expected: FAIL to compile with `cannot open include file 'Field/FieldSeeThrough.h'`.

- [ ] **Step 3: Add the option**

In `FieldRenderOptions.h`, after `EMessageBoxBackground`:

```cpp
	/** Whether the scenery standing between the eye and a party member is drawn see-through about that member. */
	enum class EFieldSeeThrough : uint8
	{
		/** As the original draws it: what stands in front hides the party. */
		Off,
		/** Outside cutscenes, a dithered hole about each member on foot in what stands in front of them (FieldSeeThrough.h). */
		On,
	};
```

At the end of `FFieldRenderOptions`, before `operator==`:

```cpp
		EFieldSeeThrough SeeThrough = EFieldSeeThrough::On;
```

- [ ] **Step 4: Write the header**

`Source/WeltallCore/Public/Field/FieldSeeThrough.h`:

```cpp
#pragma once

#include "Foundation/Types.h"

#include <array>
#include <cstddef>
#include <span>

namespace Weltall
{
	struct FEventContext;

	/**
	 * A party member's sprite as the scenery in front of it is seen through (EFieldSeeThrough): the middles of its
	 * feet and of its head and its half width, in view space (x right, y down, z ahead). The hole is a capsule between
	 * them on the screen, widened by FieldSeeThrough::Widen and cut off at the feet.
	 */
	struct FFieldSeeThroughHole
	{
		std::array<float, 3> Feet{};
		std::array<float, 3> Head{};
		float HalfWidth = 0.0f;
	};

	/** The most holes a frame draws: the party's three, and the second pass of each that wades. */
	inline constexpr std::size_t FieldSeeThroughHoles = 6;

	/** The hole's shape. FieldScene.usf's SEE_THROUGH_ defines repeat these values. */
	namespace FieldSeeThrough
	{
		/** How much nearer than the sprite (view units) the scenery must stand before it is seen through: two entries. */
		inline constexpr float DepthMargin = 32.0f;
		/** Over how much more depth it fades from drawn to seen through. */
		inline constexpr float DepthFade = 64.0f;
		/** How much wider than the sprite the hole is. */
		inline constexpr float Widen = 1.75f;
		/** The share of the hole's radius its edge fades over. */
		inline constexpr float Edge = 0.35f;
		/** The share of the sprite's height above its feet the hole fades in over. */
		inline constexpr float FeetFade = 0.1f;
		/** The share of the sprite's height the hole reaches above its head. */
		inline constexpr float HeadRoom = 0.15f;
	}

	/** Whether the actor stands for one of the party's filled slots. */
	WELTALLCORE_API bool IsPartyActor(const FEventContext& Context, int32 Actor);

	/** The hole about a sprite whose quads' corners in view space are Corners: none (zero width) for no corners. */
	WELTALLCORE_API FFieldSeeThroughHole SeeThroughHoleOf(std::span<const std::array<float, 3>> Corners);

	/** How much of the scenery at View (view space) the hole takes away: 0 none, 1 all. FieldScene.usf's SeeThroughShare is the same. */
	WELTALLCORE_API float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View);
}
```

If `Foundation/Types.h` does not define `int32` and `uint8` (check `FieldPartyWatch.h`, which includes it for the same reason), include what that header includes instead.

- [ ] **Step 5: Write the bodies**

`Source/WeltallCore/Private/Field/FieldSeeThrough.cpp`:

```cpp
#include "Field/FieldSeeThrough.h"

#include "Field/EventContext.h"

#include <algorithm>
#include <cmath>

namespace Weltall
{
	namespace
	{
		float Saturate(float Value)
		{
			return std::clamp(Value, 0.0f, 1.0f);
		}
	}

	bool IsPartyActor(const FEventContext& Context, int32 Actor)
	{
		for (std::size_t Slot = 0; Slot < Context.PartyActors.size(); ++Slot)
		{
			if (Context.PartyCharacters[Slot] != FEventContext::NoActor && Context.PartyActors[Slot] == Actor)
			{
				return true;
			}
		}
		return false;
	}

	FFieldSeeThroughHole SeeThroughHoleOf(std::span<const std::array<float, 3>> Corners)
	{
		FFieldSeeThroughHole Hole;
		if (Corners.empty())
		{
			return Hole;
		}
		std::array<float, 3> Low = Corners.front();
		std::array<float, 3> High = Corners.front();
		for (const std::array<float, 3>& Corner : Corners)
		{
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Low[Axis] = std::min(Low[Axis], Corner[Axis]);
				High[Axis] = std::max(High[Axis], Corner[Axis]);
			}
		}
		const float Across = (Low[0] + High[0]) * 0.5f;
		const float Ahead = (Low[2] + High[2]) * 0.5f;
		// y runs down the screen: the feet are the largest.
		Hole.Feet = {Across, High[1], Ahead};
		Hole.Head = {Across, Low[1], Ahead};
		Hole.HalfWidth = (High[0] - Low[0]) * 0.5f;
		return Hole;
	}

	float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View)
	{
		using namespace FieldSeeThrough;
		if (View[2] <= 1.0f || Hole.Feet[2] <= 1.0f || Hole.Head[2] <= 1.0f)
		{
			return 0.0f;
		}
		const float Nearest = std::min(Hole.Feet[2], Hole.Head[2]);
		const float InFront = Saturate((Nearest - View[2] - DepthMargin) / DepthFade);
		// On the screen, as x / z and y / z: the point, the feet, and the way up to the head.
		const float PointX = View[0] / View[2];
		const float PointY = View[1] / View[2];
		const float FeetX = Hole.Feet[0] / Hole.Feet[2];
		const float FeetY = Hole.Feet[1] / Hole.Feet[2];
		const float UpX = Hole.Head[0] / Hole.Head[2] - FeetX;
		const float UpY = Hole.Head[1] / Hole.Head[2] - FeetY;
		const float Along = ((PointX - FeetX) * UpX + (PointY - FeetY) * UpY) / std::max(UpX * UpX + UpY * UpY, 1e-8f);
		const float AboveFeet = Saturate(Along / FeetFade);
		const float Kept = std::clamp(Along, 0.0f, 1.0f + HeadRoom);
		const float OffX = PointX - (FeetX + UpX * Kept);
		const float OffY = PointY - (FeetY + UpY * Kept);
		const float Radius = Hole.HalfWidth * Widen / Hole.Feet[2];
		const float Across = std::sqrt(OffX * OffX + OffY * OffY) / std::max(Radius, 1e-8f);
		const float Inside = Saturate((1.0f - Across) / Edge);
		return InFront * AboveFeet * Inside;
	}
}
```

- [ ] **Step 6: Mark the party's sprites**

In `FieldFrame.h`, in `FFieldCharacterView` after `bOnFloor`:

```cpp
		/** A party member's sprite outside cutscenes: the scenery in front of it may be seen through about it (EFieldSeeThrough). */
		bool bSeenThrough = false;
```

In `FieldPresenter.cpp`, add `#include "Field/FieldSeeThrough.h"`. In `DrawSprite`, after `View.Clip = Pass.Clip;`:

```cpp
		View.bSeenThrough = Context != nullptr && !CutsceneWatch.InCutscene() && IsPartyActor(*Context, Actor);
```

(`CutsceneWatch.Watch` already ran for this tick at the top of `DrawSprite`.)

- [ ] **Step 7: Run the tests to verify they pass**

Run (from `Tests/`): `cmake --build --preset windows`, then `ctest --preset windows -R "seen through|hole"`
Expected: the three new test cases PASS. Then the whole suite: `ctest --preset windows`. Expected: the usual
1964 + 3 = 1967 pass, and only the 19 aborts listed in `.agents/tests/expected-aborts.txt` fail.

- [ ] **Step 8: Commit**

```bash
git add Source/WeltallCore/Public/Field/FieldRenderOptions.h Source/WeltallCore/Public/Field/FieldSeeThrough.h Source/WeltallCore/Private/Field/FieldSeeThrough.cpp Source/WeltallCore/Public/Field/FieldFrame.h Source/WeltallCore/Private/Field/FieldPresenter.cpp Tests/WeltallCore/Field/FieldSeeThroughTests.cpp
git commit -m "Mark the party's sprites outside cutscenes, and shape a hole about each"
```

---

### Task 2: The renderer: cut the holes in the scenery

**Files:**
- Modify: `Source/Weltall/Private/Rendering/FieldDrawBuilder.h:21-46` (flag), next to the `AddFieldCharacterQuads` declaration (`:171`)
- Modify: `Source/Weltall/Public/Rendering/FieldSceneRendering.h` (`FFieldSceneDraw`, include)
- Modify: `Source/Weltall/Private/Rendering/FieldSceneMesh.cpp:104-110` (flag) and `:176` (reset)
- Modify: `Source/Weltall/Private/Rendering/FieldFrameGeometry.cpp` (new function after `AddFieldCharacterQuads`, `:205`)
- Modify: `Source/Weltall/Private/Rendering/FieldFrameDraw.cpp:246` (reset), `:297` (call)
- Modify: `Source/Weltall/Private/Rendering/FieldSceneShaders.h` (`FFieldScenePS::FParameters`)
- Modify: `Source/Weltall/Private/Rendering/FieldSceneRendering.cpp:335-460` (`AddSceneRuns`)
- Modify: `Shaders/Private/FieldScene.usf`
- Test: `Source/WeltallEditor/Private/Tests/FieldFrameRenderingTest.cpp`

**Interfaces:**
- Consumes: `FFieldSeeThroughHole`, `FieldSeeThroughHoles`, `FieldSeeThrough::*`, `SeeThroughHoleOf`, `FFieldCharacterView::bSeenThrough`, `EFieldSeeThrough` (Task 1)
- Produces:
  - `FieldPolygonFlags::SeeThrough = 64` (and `FLAG_SEE_THROUGH 64` in the shader)
  - `FFieldSceneDraw::SeeThroughHoles` (`TArray<Weltall::FFieldSeeThroughHole, TInlineAllocator<6>>`), `FFieldSceneDraw::SeeThroughObject` (`uint32`)
  - `void AddFieldSeeThroughHoles(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldRenderOptions& Options, FFieldSceneDraw& Draw)`

- [ ] **Step 0: Verify how to run the editor's rendering tests (once; nothing documents it yet)**

The rendering tests need a real GPU (no `-nullrhi`). Build the editor target in place and run the existing field tests:

```powershell
$UE = ((Get-Content local.env | Select-String '^UE_ROOT=').Line -split '=', 2)[1]
& "$UE\Engine\Build\BatchFiles\Build.bat" WeltallEditor Win64 Development "-Project=$PWD\Weltall.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\Weltall.uproject" -ExecCmds="Automation RunTests Weltall.Rendering.Field; Quit" -unattended -nopause -nosplash -nosound -log="$PWD\out\editor-tests.log"
Select-String -Path out\editor-tests.log -Pattern "Test Completed|Result=\{"
```

Expected: every `Weltall.Rendering.Field*` test reports `Result={Success}`. `Binaries/` and `Intermediate/` are
git-ignored. If this command does not work as written, find the one that does, and add it to `AGENTS.md` under
**Test** before going on: the next agent should not have to rediscover it.

- [ ] **Step 1: Write the failing rendering test**

In `FieldFrameRenderingTest.cpp`, give `FTestFrame::Render` a last parameter and pass it on:

```cpp
		TArray<FColor> Render(const FVramTexture& Vram, const FTestFrame* Previous = nullptr, float Alpha = 1.0f,
			EFieldSorting Sorting = EFieldSorting::PaintersOrder, Weltall::EFieldSeeThrough SeeThrough = Weltall::EFieldSeeThrough::Off)
		{
			...
			Options.Sorting = Sorting;
			Options.SeeThrough = SeeThrough;
```

Then add, at the end of the file before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldSeeThroughRenderingTest, "Weltall.Rendering.FieldSeeThroughShowsTheParty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFieldSeeThroughRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// A sprite 2000 units away over the screen's (152, 96)-(168, 128), behind a flat wall 1000 units away over much of
	// the screen. The sprite's second texel column, (156..159, y), is red.
	const auto Render = [&Units](bool bParty, Weltall::EFieldSeeThrough SeeThrough, EFieldSorting Sorting)
	{
		FTestFrame Test;
		Test.Sprite(152, 96, 2000);
		Test.Frame.Characters.back().bSeenThrough = bParty;
		Test.Quad(100, 60, 220, 180, FlatQuad, {0, 0, 200});
		Test.Evaluate();
		return Test.Render(Units, nullptr, 1.0f, Sorting, SeeThrough);
	};
	const auto At = [](const TArray<FColor>& Pixels, int32 X, int32 Y) { return Pixels[Y * ScreenWidth + X]; };

	for (const EFieldSorting Sorting : {EFieldSorting::PaintersOrder, EFieldSorting::DepthBufferWithScenery})
	{
		const TArray<FColor> Hidden = Render(true, Weltall::EFieldSeeThrough::Off, Sorting);
		const TArray<FColor> Seen = Render(true, Weltall::EFieldSeeThrough::On, Sorting);
		const TArray<FColor> Stranger = Render(false, Weltall::EFieldSeeThrough::On, Sorting);
		// Off, the wall hides the sprite; on, the sprite's middle shows through it; someone not of the party stays hidden.
		TestFalse(TEXT("The wall hides the sprite when see-through is off"), IsNear(At(Hidden, 157, 112), 255, 0, 0));
		TestTrue(TEXT("The sprite shows through the wall"), IsNear(At(Seen, 157, 112), 255, 0, 0));
		TestFalse(TEXT("Only the party is seen through"), IsNear(At(Stranger, 157, 112), 255, 0, 0));
		// Away from the sprite, and below its feet, the wall is drawn as before.
		TestEqual(TEXT("The wall away from the sprite is unchanged"), At(Seen, 110, 70), At(Hidden, 110, 70));
		TestEqual(TEXT("The wall below the sprite's feet is unchanged"), At(Seen, 160, 150), At(Hidden, 160, 150));
	}
	ReleaseResourceAndFlush(&Units);
	return true;
}
```

Check how the other tests in the file release `Units` and copy that exactly. If `ReleaseResourceAndFlush` is not
what they use, use what they use.

- [ ] **Step 2: Run it to verify it fails**

Run the Step 0 commands with the filter `Weltall.Rendering.FieldSeeThrough`.
Expected: FAIL at "The sprite shows through the wall" (nothing cuts the wall yet).

- [ ] **Step 3: Flag the scenery that may be seen through**

In `FieldDrawBuilder.h`, in `namespace FieldPolygonFlags` after `OneSided`:

```cpp
	/** A polygon of the map's scenery, neither its background nor a painted picture: it may be seen through about the party (EFieldSeeThrough). */
	constexpr uint32 SeeThrough = 64;
```

In `FieldSceneMesh.cpp`, `FFieldSceneMesh::AddScenery`, after the `if (Traits.bTextured) { ... }` block that sets
`State.Flags` closes:

```cpp
		const Weltall::FFieldSceneObject& Owner = Scene.Objects[Polygon.Object];
		if (!Owner.bBackground && !Owner.bPicture)
		{
			State.Flags |= FieldPolygonFlags::SeeThrough;
		}
```

Redirected copies of moved models keep their mesh polygon's number (`FFieldSceneCorner::Polygon`), so they keep the flag.

- [ ] **Step 4: Carry the holes in the draw**

In `FieldSceneRendering.h`, add `#include "Field/FieldSeeThrough.h"` and, in `FFieldSceneDraw` after `FloorUp`:

```cpp
	/**
	 * The party's sprites the scenery in front of them is seen through about, in the space of SeeThroughObject's rows
	 * (FFieldSeeThroughHole): placed by them as they are drawn, so a move of the player's view moves the holes with the
	 * sprites. None while EFieldSeeThrough is off.
	 */
	TArray<Weltall::FFieldSeeThroughHole, TInlineAllocator<Weltall::FieldSeeThroughHoles>> SeeThroughHoles;
	/** The object whose rows place the holes: the identity the characters' quads are drawn by. */
	uint32 SeeThroughObject = 0;
```

In `FieldDrawBuilder.h`, next to the declaration of `AddFieldCharacterQuads`:

```cpp
/**
 * The holes of the party's sprites the frame marks (FFieldCharacterView::bSeenThrough), where they are drawn Alpha of the
 * way from Previous, in the space of the identity's rows the quads are drawn by; none while the options' SeeThrough is off.
 */
void AddFieldSeeThroughHoles(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldRenderOptions& Options,
	FFieldSceneDraw& Draw);
```

In `FieldFrameGeometry.cpp`, after `AddFieldCharacterQuads`:

```cpp
void AddFieldSeeThroughHoles(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldRenderOptions& Options,
	FFieldSceneDraw& Draw)
{
	Draw.SeeThroughHoles.Reset();
	Draw.SeeThroughObject = static_cast<uint32>(Draw.Objects.Num() / 3 - 1);
	if (Options.SeeThrough == Weltall::EFieldSeeThrough::Off)
	{
		return;
	}
	TArray<std::array<float, 3>, TInlineAllocator<64>> Corners;
	for (int32 Number = 0; Number < static_cast<int32>(Latest.Characters.size()); ++Number)
	{
		const Weltall::FFieldCharacterView& Character = Latest.Characters[static_cast<std::size_t>(Number)];
		if (!Character.bSeenThrough || Character.bOnFloor || Draw.SeeThroughHoles.Num() == static_cast<int32>(Weltall::FieldSeeThroughHoles))
		{
			continue;
		}
		const FVector3f Offset = FieldCharacterOffset(Character, Previous, Alpha);
		Corners.Reset();
		for (const Weltall::FFieldCharacterQuad& Entry : Latest.Quads)
		{
			if (Entry.Character != Number)
			{
				continue;
			}
			for (std::size_t Corner = 0; Corner < 4; ++Corner)
			{
				const FVector3f At = ToVector(Entry.Quad.ViewCorner(Corner)) + Offset;
				Corners.Add({At.X, At.Y, At.Z});
			}
		}
		if (!Corners.IsEmpty())
		{
			Draw.SeeThroughHoles.Add(Weltall::SeeThroughHoleOf(std::span<const std::array<float, 3>>(Corners.GetData(), static_cast<std::size_t>(Corners.Num()))));
		}
	}
}
```

`ToVector` and `FieldCharacterOffset` are already used in that file. Add `#include "Field/FieldSeeThrough.h"` if the
file does not pick it up through `FieldSceneRendering.h`.

In `FieldFrameDraw.cpp`, `BuildFieldFrameDraw`: next to `Draw.SpriteModelCorners.Reset();` add `Draw.SeeThroughHoles.Reset();`,
and right after `AddFieldCharacterQuads(Latest, Before, Alpha, Mesh, Draw);` add:

```cpp
	AddFieldSeeThroughHoles(Latest, Before, Alpha, Options, Draw);
```

In `FieldSceneMesh.cpp`, `BuildFieldSceneDraw`, next to `Draw.bDepthBuffer = ...` (`:176`) add `Draw.SeeThroughHoles.Reset();`.
A scenery-only draw has no party.

- [ ] **Step 5: Bind the holes, placed in the view the frame is shown in**

In `FieldSceneShaders.h`, at the end of `FFieldScenePS::FParameters`:

```cpp
		SHADER_PARAMETER_ARRAY(FVector4f, SeeThroughFeet, [6])
		SHADER_PARAMETER_ARRAY(FVector4f, SeeThroughHead, [6])
		SHADER_PARAMETER(uint32, SeeThroughCount)
```

with, below the class, `static_assert(Weltall::FieldSeeThroughHoles == 6, "FieldScene.usf and FFieldScenePS hold six see-through holes");`.

In `FieldSceneRendering.cpp`, in the anonymous namespace before `AddSceneRuns`:

```cpp
	/** A frame's see-through holes where its objects' rows, as they are drawn, place them: feet and half width, then head. */
	struct FSeeThroughShapes
	{
		FVector4f Feet[Weltall::FieldSeeThroughHoles];
		FVector4f Head[Weltall::FieldSeeThroughHoles];
		uint32 Count = 0;
	};

	FSeeThroughShapes SeeThroughShapesOf(const FFieldSceneDraw& Draw)
	{
		FSeeThroughShapes Shapes;
		const int32 Row = static_cast<int32>(Draw.SeeThroughObject) * 3;
		// A scene stretched before a picture moves its sprites by anchors the rows do not follow.
		if (Draw.PictureStretch.bStaged || !Draw.Objects.IsValidIndex(Row + 2))
		{
			return Shapes;
		}
		const auto Place = [&Draw, Row](const std::array<float, 3>& Point)
		{
			const auto Dot = [&Point](const FVector4f& R) { return R.X * Point[0] + R.Y * Point[1] + R.Z * Point[2] + R.W; };
			return FVector3f(Dot(Draw.Objects[Row]), Dot(Draw.Objects[Row + 1]), Dot(Draw.Objects[Row + 2]));
		};
		for (const Weltall::FFieldSeeThroughHole& Hole : Draw.SeeThroughHoles)
		{
			Shapes.Feet[Shapes.Count] = FVector4f(Place(Hole.Feet), Hole.HalfWidth);
			Shapes.Head[Shapes.Count] = FVector4f(Place(Hole.Head), 0.0f);
			++Shapes.Count;
		}
		return Shapes;
	}
```

In `AddSceneRuns`, before `GraphBuilder.AddPass(...)`: `const FSeeThroughShapes SeeThrough = SeeThroughShapesOf(Draw);`.
Add `SeeThrough` to the lambda's capture list, and after `PixelParameters.SampleCount = Samples;`:

```cpp
						for (uint32 Hole = 0; Hole < SeeThrough.Count; ++Hole)
						{
							PixelParameters.SeeThroughFeet[Hole] = SeeThrough.Feet[Hole];
							PixelParameters.SeeThroughHead[Hole] = SeeThrough.Head[Hole];
						}
						PixelParameters.SeeThroughCount = SeeThrough.Count;
```

The rows are read here, at drawing time, because `MoveFieldSceneView` and `UnroundFieldSceneView` move them after
the draw was built. Battles and the world map leave `SeeThroughHoles` empty, so their count is 0.

- [ ] **Step 6: Cut the holes in the shader**

In `FieldScene.usf`, with the other flags:

```hlsl
// A polygon of the map's scenery that may be seen through about the party (FieldPolygonFlags::SeeThrough).
#define FLAG_SEE_THROUGH 64

// The see-through holes' shape: FieldSeeThrough.h's values.
#define SEE_THROUGH_HOLES 6
#define SEE_THROUGH_DEPTH_MARGIN 32.0
#define SEE_THROUGH_DEPTH_FADE 64.0
#define SEE_THROUGH_WIDEN 1.75
#define SEE_THROUGH_EDGE 0.35
#define SEE_THROUGH_FEET_FADE 0.1
#define SEE_THROUGH_HEAD_ROOM 0.15
```

With the other pixel parameters (after `SampleCount`):

```hlsl
// The party's sprites the scenery in front of them is seen through about, in view space: each one's feet (w its half
// width) and head.
float4 SeeThroughFeet[SEE_THROUGH_HOLES];
float4 SeeThroughHead[SEE_THROUGH_HOLES];
uint SeeThroughCount;
```

`MainVS`: add the output `out float3 OutView : TEXCOORD3` after `OutColorFlags`, and `OutView = View;` at the end.

Before `MainPS`:

```hlsl
// How much of the scenery at View a hole takes away, 0 to 1: FieldSeeThrough.h's SeeThroughShare.
float SeeThroughShare(float4 Feet, float4 Head, float3 View)
{
	if (View.z <= 1.0 || Feet.z <= 1.0 || Head.z <= 1.0)
	{
		return 0.0;
	}
	const float InFront = saturate((min(Feet.z, Head.z) - View.z - SEE_THROUGH_DEPTH_MARGIN) / SEE_THROUGH_DEPTH_FADE);
	const float2 Point = View.xy / View.z;
	const float2 AtFeet = Feet.xy / Feet.z;
	const float2 Up = Head.xy / Head.z - AtFeet;
	const float Along = dot(Point - AtFeet, Up) / max(dot(Up, Up), 1e-8);
	const float AboveFeet = saturate(Along / SEE_THROUGH_FEET_FADE);
	const float2 Nearest = AtFeet + Up * clamp(Along, 0.0, 1.0 + SEE_THROUGH_HEAD_ROOM);
	const float Radius = Feet.w * SEE_THROUGH_WIDEN / Feet.z;
	const float Inside = saturate((1.0 - length(Point - Nearest) / max(Radius, 1e-8)) / SEE_THROUGH_EDGE);
	return InFront * AboveFeet * Inside;
}

// A 4 x 4 ordered dither's threshold at a pixel: a share above it is taken away there.
static const float Bayer4[16] = {0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0};
float DitherThreshold(uint2 Pixel)
{
	return (Bayer4[(Pixel.y & 3) * 4 + (Pixel.x & 3)] + 0.5) / 16.0;
}
```

`MainPS`: add the input `float3 ViewPosition : TEXCOORD3` after `ColorFlags`, and right after
`const FFieldPolygon Polygon = PolygonAt(PolygonIndex);`:

```hlsl
	if ((Polygon.Flags & FLAG_SEE_THROUGH) != 0 && SeeThroughCount != 0)
	{
		// The same in every pass of the polygon (its depth prepasses too), so no pass leaves depth where its colour is gone.
		float Share = 0.0;
		for (uint Hole = 0; Hole < SeeThroughCount; ++Hole)
		{
			Share = max(Share, SeeThroughShare(SeeThroughFeet[Hole], SeeThroughHead[Hole], ViewPosition));
		}
		if (Share > DitherThreshold(uint2(SvPosition.xy)))
		{
			discard;
		}
	}
```

The discard has to happen in every pass, including the depth-only ones (`EFieldRunDepth::Mask*`): if the scenery's
prepass still wrote its depth, the sprite behind it would fail the depth test and stay hidden. All passes use this
one pixel shader, so putting the test at the top of `MainPS` covers them all.

- [ ] **Step 7: Run the rendering tests to verify they pass**

Run the Step 0 commands with the filter `Weltall.Rendering`.
Expected: `Weltall.Rendering.FieldSeeThroughShowsTheParty` and every existing rendering test report `Result={Success}`.

- [ ] **Step 8: Commit**

```bash
git add Source/Weltall Shaders/Private/FieldScene.usf Source/WeltallEditor/Private/Tests/FieldFrameRenderingTest.cpp
git commit -m "Draw the scenery in front of the party see-through about them"
```

---

### Task 3: The option row, its setting and the README

**Files:**
- Modify: `Source/WeltallCore/Public/Menu/OptionsText.h` (after `MessageBoxBackgroundValues`, or after the last Display row item 04 left)
- Modify: `Source/WeltallCore/Private/Menu/MenuSettingsPages.cpp:156-157` (enum), the `Row` switch (`~:313`), the `Step` switch (`~:528`)
- Modify: `Source/WeltallCore/Private/Game/PlayerSettings.cpp:323` (after `field.message_box_background`)
- Test: `Tests/WeltallCore/Menu/MenuSettingsPagesTests.cpp:449-466`, `Tests/WeltallCore/Game/PlayerSettingsTests.cpp:169`, `:281`
- Modify: `README.md:38-39`

**Interfaces:**
- Consumes: `EFieldSeeThrough`, `FFieldRenderOptions::SeeThrough` (Task 1)
- Produces: `Text::Display::SeeThrough`, `Text::Display::SeeThroughValues`, `Display::SeeThrough` row, setting key `field.see_through` (`off` / `on`)

- [ ] **Step 1: Write the failing tests**

In `MenuSettingsPagesTests.cpp`, after the battle flashes block (row 17 in the test's numbering, where the window's
two rows are hidden) insert the row at 18 and move the background-pause checks from 18 to 19:

```cpp
	// The scenery in front of the party: seen through about them at first, then as the original draws it, round.
	CHECK(FSettingsPages::Row(ESettingsPage::Display, 18, Settings).Label == Text::Display::SeeThrough.Label);
	CHECK(FSettingsPages::Row(ESettingsPage::Display, 18, Settings).Value == "On");
	REQUIRE(FSettingsPages::Step(ESettingsPage::Display, 18, 1, Settings, bCustomPicked));
	CHECK(Settings.Field.SeeThrough == EFieldSeeThrough::Off);
	CHECK(FSettingsPages::Row(ESettingsPage::Display, 18, Settings).Value == "Off");
	REQUIRE(FSettingsPages::Step(ESettingsPage::Display, 18, 1, Settings, bCustomPicked));
	CHECK(Settings.Field.SeeThrough == EFieldSeeThrough::On);

	// In the background the game stops and is silent, unless set otherwise.
	CHECK(FSettingsPages::Row(ESettingsPage::Display, 19, Settings).Value == Text::On);
	REQUIRE(FSettingsPages::Step(ESettingsPage::Display, 19, 1, Settings, bCustomPicked));
```

If item 04 or anything later has added rows, recount: the test's index is the enum's index less the two hidden window rows.

In `PlayerSettingsTests.cpp`, with the defaults (`:169`):

```cpp
	CHECK(ValueOf(Defaults, "field.see_through") == "on");
```

and with the read-back checks (`:281`):

```cpp
	CHECK(ReadPlayerSettings("field.see_through = off\n").Field.SeeThrough == EFieldSeeThrough::Off);
```

- [ ] **Step 2: Run them to verify they fail**

Run (from `Tests/`): `cmake --build --preset windows`
Expected: FAIL to compile, `'SeeThrough': is not a member of 'Weltall::OptionsText::Display'`.

- [ ] **Step 3: Add the text, the row and the setting**

`OptionsText.h`:

```cpp
		inline constexpr FRow SeeThrough{"See-through scenery", {"Shows the party through", "trees and walls in front of", "them. Off draws them hidden."}};
		inline constexpr std::array<std::string_view, 2> SeeThroughValues{"Off", "On"};
```

`MenuSettingsPages.cpp`, in the `Display` enum, insert `SeeThrough` between `BattleFlashes` and `BackgroundPause`. In the `Row` switch:

```cpp
			case Display::SeeThrough:
				return Shown(Text::Display::SeeThrough, ChoiceName(Settings.Field.SeeThrough, Text::Display::SeeThroughValues));
```

In the `Step` switch:

```cpp
			case Display::SeeThrough:
				return StepChoice(Settings.Field.SeeThrough, Direction, 2);
```

`PlayerSettings.cpp`, after `field.message_box_background`:

```cpp
			Visitor.Choice("field.see_through", Field.SeeThrough, {"off", "on"});
```

- [ ] **Step 4: Run the tests to verify they pass**

Run (from `Tests/`): `cmake --build --preset windows`, then `ctest --preset windows`.
Expected: all pass except the 19 listed in `.agents/tests/expected-aborts.txt`.

- [ ] **Step 5: Describe it for players**

`README.md`, after the **A free camera** bullet:

```markdown
- **See-through scenery**: trees, rocks and walls between the camera and your party are thinned out around them, so
  you can see where you walk. Cutscenes keep the original framing. **Options → Display → See-through scenery**.
```

- [ ] **Step 6: Commit**

```bash
git add Source/WeltallCore/Public/Menu/OptionsText.h Source/WeltallCore/Private/Menu/MenuSettingsPages.cpp Source/WeltallCore/Private/Game/PlayerSettings.cpp Tests/WeltallCore/Menu/MenuSettingsPagesTests.cpp Tests/WeltallCore/Game/PlayerSettingsTests.cpp README.md
git commit -m "Add a See-through scenery row under Options, Display"
```

---

### Task 4: Build, play, tune, and land as one commit

**Files:**
- Create: `.agents/validation/m02-see-through.md`
- Modify: `.agents/plans/progress.json` (item 05)
- Possibly modify: the `FieldSeeThrough` constants in `FieldSeeThrough.h` **and** `FieldScene.usf` together

- [ ] **Step 1: Build the package**

Run: `.\Scripts\build.ps1`
Expected: it ends with `Development: ran for ... s` and `out/builds/Development-check.log` has the usual lines (the game
is running, the story documents were read, `Found 2 of the game's discs`).

- [ ] **Step 2: Have the owner play it**

Ask the owner to play from `D:\Games\Weltall` on maps they named as a problem, and to check:
- a party member walking behind a tree, a rock and a pillar shows through it, with a soft dithered edge;
- a member standing against a wall keeps the wall behind them, and the floor in front of their feet is not cut;
- turning the free camera keeps the hole on the member;
- a cutscene shows no holes;
- Off in Options gives the original picture back at once.

If the hole is too small or too large, or cuts in too early, change `Widen`, `Edge`, `DepthMargin` or `DepthFade`
in **both** `FieldSeeThrough.h` and `FieldScene.usf`, rerun Task 1 Step 7 and Task 2 Step 7 (fix the expected values
in the tests if a constant they test moved), and rebuild.

- [ ] **Step 3: Record the evidence**

`.agents/validation/m02-see-through.md`: what was run (the test commands and their pass counts, the build, the maps
played), what the owner accepted, the constants' final values, and the known gaps listed at the top of this plan.
In `progress.json`, add item 05 (`"title": "See-through scenery about the party on the field"`, the files of this plan,
`"verify"`: the owner plays the listed checks from `D:\Games\Weltall`, `"evidence"`: the validation note), status `done`
once the owner has accepted it.

- [ ] **Step 4: Land as one commit**

The definition of done asks for one commit per item. With the owner's go-ahead, squash the branch onto `main`:

```bash
git switch main
git merge --squash see-through-scenery
git commit -m "Let the party be seen through the scenery that stands in front of them"
```

Push only when the owner asks.
