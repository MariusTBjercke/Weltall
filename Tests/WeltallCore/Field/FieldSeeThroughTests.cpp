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
	// The one the player walks is seen through too, whatever stands in its slot (a gear, say).
	Context.LeaderActor = 11;
	CHECK(IsSeenThroughActor(Context, 11));
	CHECK(IsSeenThroughActor(Context, 4));
	CHECK_FALSE(IsSeenThroughActor(Context, 5));
}

TEST_CASE("A sprite's hole runs from its feet to its head, as wide as its quads", "[Field]")
{
	const FFieldSeeThroughHole Hole = StandingSprite();
	CHECK(Hole.Feet == std::array<float, 3>{0.0f, 0.0f, 1000.0f});
	CHECK(Hole.Head == std::array<float, 3>{0.0f, -32.0f, 1000.0f});
	CHECK(Hole.HalfWidth == 8.0f);
	CHECK(SeeThroughHoleOf({}).HalfWidth == 0.0f);
}

TEST_CASE("The scenery is seen through in a round bubble about the sprite, in front of it and above its floor", "[Field]")
{
	const FFieldSeeThroughHole Hole = StandingSprite();
	// The world's up is up the screen: y runs down.
	const std::array<float, 3> Up{0.0f, -1.0f, 0.0f};
	// At its middle, well in front: all of it.
	CHECK(SeeThroughShare(Hole, {0.0f, -8.0f, 500.0f}, Up) == Catch::Approx(1.0f));
	// Behind the sprite, or in front by less than the margin (a wall it stands against): none.
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 1500.0f}, Up) == 0.0f);
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 980.0f}, Up) == 0.0f);
	// Half way through the depth fade: half.
	CHECK(SeeThroughShare(Hole, {0.0f, -16.0f, 936.0f}, Up) == Catch::Approx(0.5f).margin(0.01));
	// Far to its side: none. The ground before its feet, lower than they are: none.
	CHECK(SeeThroughShare(Hole, {500.0f, -16.0f, 500.0f}, Up) == 0.0f);
	CHECK(SeeThroughShare(Hole, {0.0f, 4.0f, 500.0f}, Up) == 0.0f);
	// Level with its middle, at nine tenths of the bubble's radius (Widen half heights): the soft edge.
	const float Radius = 0.016f * FieldSeeThrough::Widen;
	CHECK(SeeThroughShare(Hole, {0.9f * Radius * 500.0f, -8.0f, 500.0f}, Up) == Catch::Approx(0.1f / FieldSeeThrough::Edge).margin(0.01));
	// The bubble is round: as far straight above its middle is the same.
	CHECK(SeeThroughShare(Hole, {0.0f, (-0.016f - 0.9f * Radius) * 500.0f, 500.0f}, Up) == Catch::Approx(0.1f / FieldSeeThrough::Edge).margin(0.01));
	// Behind the eye: none.
	CHECK(SeeThroughShare(Hole, {0.0f, -8.0f, -10.0f}, Up) == 0.0f);
	// Without the world's up, what lies below its feet on the screen is kept.
	CHECK(SeeThroughShare(Hole, {0.0f, -8.0f, 500.0f}, {}) == Catch::Approx(1.0f));
	CHECK(SeeThroughShare(Hole, {0.0f, 4.0f, 500.0f}, {}) == 0.0f);
}

TEST_CASE("The eye looks for a sprite up its middle and to its sides", "[Field]")
{
	const auto Samples = SeeThroughSamples(StandingSprite());
	CHECK(Samples[0] == std::array<float, 3>{0.0f, -8.0f, 1000.0f});
	CHECK(Samples[1][1] == Catch::Approx(-17.6f));
	CHECK(Samples[2][1] == Catch::Approx(-27.2f));
	CHECK(Samples[3][0] == Catch::Approx(-4.8f));
	CHECK(Samples[4][0] == Catch::Approx(4.8f));
}

TEST_CASE("Only what stands between the eye and a point hides it", "[Field]")
{
	const std::array<float, 3> To{0.0f, 0.0f, 1000.0f};
	// A wall across the ray, half way: it hides the point, whichever way it faces.
	CHECK(HidesFromEye(To, {-100.0f, -100.0f, 500.0f}, {100.0f, -100.0f, 500.0f}, {0.0f, 100.0f, 500.0f}));
	CHECK(HidesFromEye(To, {-100.0f, -100.0f, 500.0f}, {0.0f, 100.0f, 500.0f}, {100.0f, -100.0f, 500.0f}));
	// Beside the ray, behind the point, or in front of it by less than the margin (what it stands against): it does not.
	CHECK_FALSE(HidesFromEye(To, {200.0f, -100.0f, 500.0f}, {400.0f, -100.0f, 500.0f}, {300.0f, 100.0f, 500.0f}));
	CHECK_FALSE(HidesFromEye(To, {-100.0f, -100.0f, 1500.0f}, {100.0f, -100.0f, 1500.0f}, {0.0f, 100.0f, 1500.0f}));
	CHECK_FALSE(HidesFromEye(To, {-100.0f, -100.0f, 990.0f}, {100.0f, -100.0f, 990.0f}, {0.0f, 100.0f, 990.0f}));
	// Along the ray, edge on: it does not.
	CHECK_FALSE(HidesFromEye(To, {0.0f, -100.0f, 400.0f}, {0.0f, 100.0f, 400.0f}, {0.0f, 0.0f, 600.0f}));
}

TEST_CASE("The ground under a sprite's feet bears it, and nothing else does", "[Field]")
{
	const FFieldSeeThroughHole Hole = StandingSprite();
	const std::array<float, 3> Up{0.0f, -1.0f, 0.0f};
	// A floor under its feet, level or a little below them.
	CHECK(BearsSprite(Hole, Up, {-50.0f, 0.0f, 950.0f}, {50.0f, 0.0f, 950.0f}, {0.0f, 0.0f, 1100.0f}));
	CHECK(BearsSprite(Hole, Up, {-50.0f, 4.0f, 950.0f}, {50.0f, 4.0f, 950.0f}, {0.0f, 4.0f, 1100.0f}));
	// A floor beside it, a ceiling over its head, a wall in front of it: none bears it.
	CHECK_FALSE(BearsSprite(Hole, Up, {150.0f, 0.0f, 950.0f}, {250.0f, 0.0f, 950.0f}, {200.0f, 0.0f, 1100.0f}));
	CHECK_FALSE(BearsSprite(Hole, Up, {-50.0f, -40.0f, 950.0f}, {50.0f, -40.0f, 950.0f}, {0.0f, -40.0f, 1100.0f}));
	CHECK_FALSE(BearsSprite(Hole, Up, {-100.0f, -100.0f, 500.0f}, {100.0f, -100.0f, 500.0f}, {0.0f, 100.0f, 500.0f}));
}
