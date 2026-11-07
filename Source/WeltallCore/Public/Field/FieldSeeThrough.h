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

	/** Whether the scenery in front of the actor may be seen through: one of the party's, or the one the player controls. */
	WELTALLCORE_API bool IsSeenThroughActor(const FEventContext& Context, int32 Actor);

	/** The hole about a sprite whose quads' corners in view space are Corners: none (zero width) for no corners. */
	WELTALLCORE_API FFieldSeeThroughHole SeeThroughHoleOf(std::span<const std::array<float, 3>> Corners);

	/** How much of the scenery at View (view space) the hole takes away: 0 none, 1 all. FieldScene.usf's SeeThroughShare is the same. */
	WELTALLCORE_API float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View);
}
