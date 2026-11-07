#include "Field/FieldSeeThrough.h"

#include "Field/EventContext.h"

#include <algorithm>
#include <bit>
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

	bool IsSeenThroughActor(const FEventContext& Context, int32 Actor)
	{
		return IsPartyActor(Context, Actor) || Actor == Context.LeaderActor;
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

	std::array<std::array<float, 3>, FieldSeeThroughSamples> SeeThroughSamples(const FFieldSeeThroughHole& Hole)
	{
		const auto Along = [&Hole](float Share, float Side)
		{
			std::array<float, 3> Point{};
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Point[Axis] = Hole.Feet[Axis] + (Hole.Head[Axis] - Hole.Feet[Axis]) * Share;
			}
			Point[0] += Side * Hole.HalfWidth;
			return Point;
		};
		std::array<std::array<float, 3>, FieldSeeThroughSamples> Samples{};
		constexpr std::array<float, 5> Heights{0.15f, 0.35f, 0.55f, 0.75f, 0.95f};
		constexpr std::array<float, 3> Sides{-0.6f, 0.0f, 0.6f};
		for (std::size_t Row = 0; Row < Heights.size(); ++Row)
		{
			for (std::size_t Column = 0; Column < Sides.size(); ++Column)
			{
				Samples[Row * Sides.size() + Column] = Along(Heights[Row], Sides[Column]);
			}
		}
		return Samples;
	}

	bool SeeThroughCoversEnough(uint32 Hidden)
	{
		constexpr uint32 All = (1u << FieldSeeThroughSamples) - 1;
		return static_cast<float>(std::popcount(Hidden & All)) >= FieldSeeThrough::Coverage * static_cast<float>(FieldSeeThroughSamples);
	}

	namespace
	{
		using FPoint = std::array<float, 3>;

		FPoint Sub(const FPoint& X, const FPoint& Y) { return {X[0] - Y[0], X[1] - Y[1], X[2] - Y[2]}; }
		float Dot(const FPoint& X, const FPoint& Y) { return X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2]; }
		FPoint Cross(const FPoint& X, const FPoint& Y) { return {X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0]}; }

		/** Where the stretch From to To crosses the triangle A B C, as a share of the way (Moller and Trumbore); below 0 for nowhere. */
		float Crossing(const FPoint& From, const FPoint& To, const FPoint& A, const FPoint& B, const FPoint& C)
		{
			const FPoint Way = Sub(To, From);
			const FPoint EdgeB = Sub(B, A);
			const FPoint EdgeC = Sub(C, A);
			const FPoint Across = Cross(Way, EdgeC);
			const float Determinant = Dot(EdgeB, Across);
			if (std::abs(Determinant) < 1e-6f)
			{
				return -1.0f;
			}
			const float Inverse = 1.0f / Determinant;
			const FPoint FromA = Sub(From, A);
			const float U = Dot(FromA, Across) * Inverse;
			if (U < 0.0f || U > 1.0f)
			{
				return -1.0f;
			}
			const FPoint Up = Cross(FromA, EdgeB);
			const float V = Dot(Way, Up) * Inverse;
			if (V < 0.0f || U + V > 1.0f)
			{
				return -1.0f;
			}
			const float Share = Dot(EdgeC, Up) * Inverse;
			return Share >= 0.0f && Share <= 1.0f ? Share : -1.0f;
		}
	}

	bool HidesFromEye(const std::array<float, 3>& To, const std::array<float, 3>& A, const std::array<float, 3>& B, const std::array<float, 3>& C)
	{
		const float Distance = std::sqrt(Dot(To, To));
		if (Distance <= FieldSeeThrough::DepthMargin)
		{
			return false;
		}
		const float Share = Crossing({0.0f, 0.0f, 0.0f}, To, A, B, C);
		return Share > 0.0f && Share < 1.0f - FieldSeeThrough::DepthMargin / Distance;
	}

	bool BearsSprite(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& Up, const std::array<float, 3>& A, const std::array<float, 3>& B,
		const std::array<float, 3>& C)
	{
		// A third of its height above and below its feet: the ground it stands on, however the floor slopes.
		const float Reach = std::sqrt(Dot(Sub(Hole.Head, Hole.Feet), Sub(Hole.Head, Hole.Feet))) / 3.0f;
		const FPoint Above{Hole.Feet[0] + Up[0] * Reach, Hole.Feet[1] + Up[1] * Reach, Hole.Feet[2] + Up[2] * Reach};
		const FPoint Below{Hole.Feet[0] - Up[0] * Reach, Hole.Feet[1] - Up[1] * Reach, Hole.Feet[2] - Up[2] * Reach};
		return Crossing(Above, Below, A, B, C) >= 0.0f;
	}

	float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View, const std::array<float, 3>& Up, float Widen)
	{
		using namespace FieldSeeThrough;
		if (View[2] <= 1.0f || Hole.Feet[2] <= 1.0f || Hole.Head[2] <= 1.0f)
		{
			return 0.0f;
		}
		const float Nearest = std::min(Hole.Feet[2], Hole.Head[2]);
		const float InFront = Saturate((Nearest - View[2] - DepthMargin) / DepthFade);
		// Above the floor the sprite stands on, by the world's up; without it, above its feet on the screen.
		const float Height = std::sqrt((Hole.Head[0] - Hole.Feet[0]) * (Hole.Head[0] - Hole.Feet[0]) + (Hole.Head[1] - Hole.Feet[1]) * (Hole.Head[1] - Hole.Feet[1])
			+ (Hole.Head[2] - Hole.Feet[2]) * (Hole.Head[2] - Hole.Feet[2]));
		float Above = 0.0f;
		if (Up[0] != 0.0f || Up[1] != 0.0f || Up[2] != 0.0f)
		{
			Above = (View[0] - Hole.Feet[0]) * Up[0] + (View[1] - Hole.Feet[1]) * Up[1] + (View[2] - Hole.Feet[2]) * Up[2];
		}
		else
		{
			// y runs down the screen: how far above the feet, at the feet's depth.
			Above = (Hole.Feet[1] / Hole.Feet[2] - View[1] / View[2]) * Hole.Feet[2];
		}
		const float AboveFloor = Saturate((Above - FloorMargin * Height) / std::max(FloorFade * Height, 1e-4f));
		// On the screen, as x / z and y / z: the point, and the bubble about the sprite's middle.
		const float PointX = View[0] / View[2];
		const float PointY = View[1] / View[2];
		const float FeetX = Hole.Feet[0] / Hole.Feet[2];
		const float FeetY = Hole.Feet[1] / Hole.Feet[2];
		const float HeadX = Hole.Head[0] / Hole.Head[2];
		const float HeadY = Hole.Head[1] / Hole.Head[2];
		const float MiddleX = (FeetX + HeadX) * 0.5f;
		const float MiddleY = (FeetY + HeadY) * 0.5f;
		const float HalfHeight = std::sqrt((HeadX - FeetX) * (HeadX - FeetX) + (HeadY - FeetY) * (HeadY - FeetY)) * 0.5f;
		const float Radius = std::max(HalfHeight, Hole.HalfWidth / Hole.Feet[2]) * Widen;
		const float Across = std::sqrt((PointX - MiddleX) * (PointX - MiddleX) + (PointY - MiddleY) * (PointY - MiddleY)) / std::max(Radius, 1e-8f);
		const float Inside = Saturate((1.0f - Across) / Edge);
		return InFront * AboveFloor * Inside;
	}

	std::span<const float> FFieldSeeThroughFade::Step(std::span<const bool> Hiding, double Now, uint32 Tick)
	{
		using namespace FieldSeeThrough;
		if (!bStarted || Hiding.size() != Shares.size() || Tick < LastTick)
		{
			Shares.assign(Hiding.size(), 0.0f);
			Held.assign(Hiding.size(), 0.0f);
			Last = Now;
			bStarted = true;
		}
		// A long stall (a load, the window in the background) is not a fade's worth of time.
		const float Seconds = static_cast<float>(std::clamp(Now - Last, 0.0, 0.1));
		Last = Now;
		LastTick = Tick;
		for (std::size_t Model = 0; Model < Hiding.size(); ++Model)
		{
			Held[Model] = Hiding[Model] ? Held[Model] + Seconds : 0.0f;
			const bool bShown = Hiding[Model] && Held[Model] >= Delay;
			Shares[Model] = bShown ? std::min(1.0f, Shares[Model] + Seconds / FadeIn) : std::max(0.0f, Shares[Model] - Seconds / FadeOut);
		}
		return Shares;
	}
}
