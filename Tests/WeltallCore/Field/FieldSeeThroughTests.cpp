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
