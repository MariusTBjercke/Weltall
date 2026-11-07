#pragma once

#include "Foundation/Types.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace Weltall
{
	struct FEventContext;

	/**
	 * A party member's sprite as the scenery in front of it is seen through (EFieldSeeThrough): the middles of its
	 * feet and of its head and its half width, in view space (x right, y down, z ahead). The hole is a round bubble on the
	 * screen about its middle, FieldSeeThrough::Widen times its half height across, taking away only what stands above
	 * the floor it stands on, so the ground before its feet stays.
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
		/** The bubble's radius over the sprite's half height (or half width, when wider). */
		inline constexpr float Widen = 3.0f;
		/** The share of the bubble's radius its edge fades over. */
		inline constexpr float Edge = 0.35f;
		/** How far above the sprite's floor, as shares of its height, the scenery starts to be taken away, and over how much more it fades in. */
		inline constexpr float FloorMargin = 0.05f;
		inline constexpr float FloorFade = 0.15f;
		/** How long (seconds) a model must hide the party before it starts to be seen through: a jump past a ledge never does. */
		inline constexpr float Delay = 0.2f;
		/** How long it takes to be seen through all the way, and to come back. */
		inline constexpr float FadeIn = 0.25f;
		inline constexpr float FadeOut = 0.35f;
	}

	/**
	 * How far each placed model of a map is seen through, from frame to frame: it starts once the model has hidden the
	 * party for FieldSeeThrough::Delay, and comes in and goes out over FadeIn and FadeOut, rather than at once.
	 */
	class FFieldSeeThroughFade
	{
	public:
		/**
		 * The shares (0 to 1) after a frame shown at Now (seconds) in which Hiding tells which models hide the party, of
		 * the field's tick Tick. Another count of models, or a tick before the last (another map), starts again from none.
		 */
		WELTALLCORE_API std::span<const float> Step(std::span<const bool> Hiding, double Now, uint32 Tick);

	private:
		std::vector<float> Shares;
		/** How long each has hidden the party without a break. */
		std::vector<float> Held;
		double Last = 0.0;
		uint32 LastTick = 0;
		bool bStarted = false;
	};

	/** Whether the actor stands for one of the party's filled slots. */
	WELTALLCORE_API bool IsPartyActor(const FEventContext& Context, int32 Actor);

	/** Whether the scenery in front of the actor may be seen through: one of the party's, or the one the player controls. */
	WELTALLCORE_API bool IsSeenThroughActor(const FEventContext& Context, int32 Actor);

	/** The hole about a sprite whose quads' corners in view space are Corners: none (zero width) for no corners. */
	WELTALLCORE_API FFieldSeeThroughHole SeeThroughHoleOf(std::span<const std::array<float, 3>> Corners);

	/** How many points of a sprite the eye looks for (SeeThroughSamples). */
	inline constexpr std::size_t FieldSeeThroughSamples = 5;

	/**
	 * The points of a hole's sprite the eye looks for, in view space: up its middle from near its feet to near its head,
	 * and to either side of its middle. A placed model of the scenery that stands between the eye and one of them hides
	 * the sprite, and only such a model is seen through.
	 */
	WELTALLCORE_API std::array<std::array<float, 3>, FieldSeeThroughSamples> SeeThroughSamples(const FFieldSeeThroughHole& Hole);

	/**
	 * Whether the triangle A B C (view space, either side facing the eye) stands between the eye, at the origin, and
	 * the point To, nearer than To by more than FieldSeeThrough::DepthMargin.
	 */
	WELTALLCORE_API bool HidesFromEye(const std::array<float, 3>& To, const std::array<float, 3>& A, const std::array<float, 3>& B, const std::array<float, 3>& C);

	/**
	 * Whether the triangle A B C crosses the sprite's footing: a short stretch of the world's up (Up, unit length, view
	 * space) through its feet. The placed model it belongs to is what the sprite stands on, never seen through.
	 */
	WELTALLCORE_API bool BearsSprite(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& Up, const std::array<float, 3>& A, const std::array<float, 3>& B,
		const std::array<float, 3>& C);

	/**
	 * How much of the scenery at View (view space) the hole takes away: 0 none, 1 all. Up is the world's up in view space
	 * (unit length); without one (zero), what lies below the sprite's feet on the screen is kept instead. FieldScene.usf's
	 * SeeThroughShare is the same.
	 */
	WELTALLCORE_API float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View, const std::array<float, 3>& Up);
}
