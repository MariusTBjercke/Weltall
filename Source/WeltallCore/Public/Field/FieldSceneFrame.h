#pragma once

#include "Field/FieldCamera.h"
#include "Field/FieldScene.h"
#include "Foundation/Types.h"
#include "Graphics/AffineBlend.h"
#include "Graphics/Gte.h"
#include "Graphics/Texture.h"

#include <array>
#include <vector>

namespace Weltall
{
	/** Which rules decide what a frame of the scenery draws. */
	enum class EFieldCulling : uint8
	{
		/**
		 * The original's, made for its 320-pixel screen: models whose bounds leave the screen, and
		 * polygons with no corner on the screen across or down, facing away, or past the far bucket.
		 * A corner the GTE cannot project (behind the eye, too near) stays where its division left it.
		 */
		Original,
		/**
		 * Everything a view of any width can show: only hidden models, polygons facing away and those
		 * past the far bucket are left out; the drawing clips the rest.
		 */
		InRange,
	};

	/** Why the original leaves a polygon out of a frame: none of these, and it is drawn. */
	namespace FieldPolygonDrop
	{
		/** Its model is hidden, or its bounds are off the screen. */
		constexpr uint8 Model = 1 << 0;
		/** No corner across the screen, or none down it. */
		constexpr uint8 OffScreen = 1 << 2;
		constexpr uint8 BackFace = 1 << 3;
		/** A depth of zero, which the kernel does not link into the ordering table (but for a flat triangle it lights as it draws). */
		constexpr uint8 ZeroDepth = 1 << 4;
		constexpr uint8 BeyondFarBucket = 1 << 5;
	}

	/** A placed model in one frame. */
	struct FFieldObjectView
	{
		/** Model to view space (a view unit is a third of a world unit), as the GTE holds it for the model. */
		FGteMatrix Matrix;
		/**
		 * The same composed without the GTE's rounding (FieldObjectAffine): what the scene is drawn with, so that pieces
		 * of different models meeting stay met as the view moves.
		 */
		FAffine Exact;
		/** Hidden, or its bounds off the original's screen. */
		bool bCulled = false;
		/** It stands as the map's data places it (FFieldSceneObject::IsAsBuilt): the scene's joins hold for it. */
		bool bAsBuilt = false;
	};

	/** A polygon in one frame. */
	struct FFieldPolygonView
	{
		/** Each corner on the 320 x 224 screen, as the GTE projects it. */
		std::array<int16, 4> ScreenX{};
		std::array<int16, 4> ScreenY{};
		/** The ordering-table entry it is sorted into: its depth / 16. */
		uint16 Bucket = 0;
		/** FieldPolygonDrop bits: zero when the original draws it. */
		uint8 Dropped = 0;
		/** Drawn under the rules the frame was evaluated with. */
		bool bDrawn = false;
		/** Its colors this frame, one per corner for Gouraud kinds: as built, lit again, or depth-cued in fog. */
		std::array<FMdlColor, 4> Colors{};
		/** Depth-cued in fog: its texels are modulated by its colour even when its packet draws them raw. */
		bool bFogged = false;
		/**
		 * The blend mode it would use if untextured and semi-transparent: the GPU takes it from the
		 * page of the last textured polygon drawn before it, or the drawing environment's (average).
		 */
		EBlendMode UntexturedBlend = EBlendMode::Average;
	};

	/**
	 * One frame of a map's scenery as the field draws it: every placed model's matrix, every
	 * polygon's projection, sort key and colors, and the order the ordering tables draw them in.
	 * Kept from frame to frame, it allocates only when the scene changes size.
	 */
	struct FFieldSceneFrame
	{
		std::vector<FFieldObjectView> Objects;
		std::vector<FFieldPolygonView> Polygons;
		/**
		 * The polygons drawn, in the order the GPU draws them: the background table's, then the main
		 * one's, each from the far bucket to the nearest, and within a bucket the last linked first.
		 */
		std::vector<uint32> DrawOrder;
		/** Every placed model's corners, projected. */
		std::vector<FGteProjection> Corners;
		/** Sorting scratch: polygons per bucket of both tables. */
		std::vector<uint32> BucketEnds;
	};

	/** What the kernel's drawing makes of a polygon whose corners the GTE projected. */
	struct FFieldPolygonTest
	{
		/** The ordering-table entry it is sorted into. */
		uint16 Bucket = 0;
		/** FieldPolygonDrop bits of its own (screen, facing, zero depth), its model's and the far bucket's aside. */
		uint8 Dropped = 0;
		/**
		 * Whether a view of any width draws it: its first three corners run clockwise seen from the eye, in view space,
		 * and, where the original finds no corner of it on its screen, it lies wholly beyond one of the screen's edges
		 * (what a wider view adds), not across the screen, which the original never shows (the camera's own side of a
		 * plane it pulls back through).
		 */
		bool bInRange = false;
		/**
		 * As bInRange without its facing: what the field draws under a view of any width, leaving the facing to the
		 * drawing, which tests it in every frame against the view it draws with (a free camera turns between ticks).
		 */
		bool bInRangeAnyFacing = false;
	};

	/** The kernel's shift from a depth to its ordering-table entry, as the field keeps it. */
	inline constexpr int32 FieldOrderingShift = 2;

	/** The rows of the kernel's screen clip as the field sets it (240 lines): a corner counts down the screen below it. */
	inline constexpr uint16 FieldClipRows = 239;

	/** The columns of the kernel's screen clip as the field sets it: a corner counts across the screen below it. */
	inline constexpr uint16 FieldClipColumns = 320;

	/**
	 * The kernel's rules for one polygon of a model, its corners projected in Projected by the
	 * polygon's vertex numbers: the bucket by the draw mode (the average depth's OTZ, or the farthest
	 * or nearest corner's depth / 4, shifted by OrderingShift), and the reasons the original leaves it out,
	 * its screen ClipRows high (a program's display's lines less one) and ClipColumns wide.
	 */
	WELTALLCORE_API FFieldPolygonTest TestFieldPolygon(const FMdlPrimitive& Primitive, const FGteProjection* Projected, EFieldDrawMode Mode, const FGte& Gte,
		int32 OrderingShift = FieldOrderingShift, uint16 ClipRows = FieldClipRows, uint16 ClipColumns = FieldClipColumns);

	/** The GTE as the field sets it up for drawing: its screen center, H, and the average-depth scales. */
	WELTALLCORE_API FGte FieldDrawingGte(const FFieldView& View);

	/** The matrix a placed model is drawn with in a view. */
	WELTALLCORE_API FGteMatrix FieldObjectMatrix(const FFieldSceneObject& Object, const FFieldView& View);

	/**
	 * FieldObjectMatrix composed in floating point: the view's matrices as the GTE holds them, then the model's own
	 * placement unrounded. The GTE rounds each model's matrix on its own, so two models' pieces that meet in the world
	 * part and cross by a fraction of a unit as the view moves; under one view composed exactly they keep their places.
	 */
	WELTALLCORE_API FAffine FieldObjectAffine(const FFieldSceneObject& Object, const FFieldView& View);

	/** Whether the field culls a placed model: its bounds' screen square outside the screen and the margins. */
	WELTALLCORE_API bool IsFieldObjectOffScreen(const FFieldSceneObject& Object, const FGteMatrix& Matrix, const FFieldRenderState& State,
		const FGte& Drawing);

	/** Evaluates a frame of the scene for a view under the given rules. */
	WELTALLCORE_API void EvaluateFieldScene(const FFieldScene& Scene, const FFieldView& View, EFieldCulling Culling, FFieldSceneFrame& Frame);
}
