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
		return {Along(0.25f, 0.0f), Along(0.55f, 0.0f), Along(0.85f, 0.0f), Along(0.55f, -0.6f), Along(0.55f, 0.6f)};
	}

	bool HidesFromEye(const std::array<float, 3>& To, const std::array<float, 3>& A, const std::array<float, 3>& B, const std::array<float, 3>& C)
	{
		// The ray from the eye to To, as Moller and Trumbore cross it with the triangle: Hit is the share of the way.
		const auto Sub = [](const std::array<float, 3>& X, const std::array<float, 3>& Y) { return std::array<float, 3>{X[0] - Y[0], X[1] - Y[1], X[2] - Y[2]}; };
		const auto Dot = [](const std::array<float, 3>& X, const std::array<float, 3>& Y) { return X[0] * Y[0] + X[1] * Y[1] + X[2] * Y[2]; };
		const auto Cross = [](const std::array<float, 3>& X, const std::array<float, 3>& Y)
		{ return std::array<float, 3>{X[1] * Y[2] - X[2] * Y[1], X[2] * Y[0] - X[0] * Y[2], X[0] * Y[1] - X[1] * Y[0]}; };
		const float Distance = std::sqrt(Dot(To, To));
		if (Distance <= FieldSeeThrough::DepthMargin)
		{
			return false;
		}
		const std::array<float, 3> EdgeB = Sub(B, A);
		const std::array<float, 3> EdgeC = Sub(C, A);
		const std::array<float, 3> Across = Cross(To, EdgeC);
		const float Determinant = Dot(EdgeB, Across);
		if (std::abs(Determinant) < 1e-6f)
		{
			return false;
		}
		const float Inverse = 1.0f / Determinant;
		const std::array<float, 3> FromA = Sub({0.0f, 0.0f, 0.0f}, A);
		const float U = Dot(FromA, Across) * Inverse;
		if (U < 0.0f || U > 1.0f)
		{
			return false;
		}
		const std::array<float, 3> Up = Cross(FromA, EdgeB);
		const float V = Dot(To, Up) * Inverse;
		if (V < 0.0f || U + V > 1.0f)
		{
			return false;
		}
		const float Hit = Dot(EdgeC, Up) * Inverse;
		return Hit > 0.0f && Hit < 1.0f - FieldSeeThrough::DepthMargin / Distance;
	}

	float SeeThroughShare(const FFieldSeeThroughHole& Hole, const std::array<float, 3>& View, const std::array<float, 3>& Up)
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
}
