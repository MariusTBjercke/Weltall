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
