#include "Field/FieldSceneFrame.h"

#include "Graphics/GteVectors.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** The culling test's screen: 320 x 224. */
		constexpr int32 CullWidth = 320;
		constexpr int32 CullHeight = 224;
		/** A relit model's light matrix is its matrix at half scale. */
		constexpr int32 LightScale = 0x800;
		constexpr std::size_t EntriesPerTable = 4096;

		struct FCorners
		{
			std::array<const FGteProjection*, 4> At{};
			int32 Count = 3;
		};

		/** The kinds the kernel's fog draw modes handle: flat-coloured textured triangles and quads. */
		bool IsFlatTextured(const FMdlPrimitive& Primitive)
		{
			switch (Primitive.Type)
			{
			case EMdlPrimitiveType::TexturedTriangle:
			case EMdlPrimitiveType::StaticTexturedTriangle:
			case EMdlPrimitiveType::TexturedQuad:
			case EMdlPrimitiveType::StaticTexturedQuad:
				return true;
			default:
				return false;
			}
		}

		FCorners CornersOf(const FMdlPrimitive& Primitive, const FGteProjection* Projected)
		{
			FCorners Corners;
			Corners.Count = Primitive.Traits().Corners;
			for (int32 Corner = 0; Corner < Corners.Count; ++Corner)
			{
				Corners.At[static_cast<std::size_t>(Corner)] = &Projected[Primitive.Vertices[static_cast<std::size_t>(Corner)]];
			}
			return Corners;
		}

		/** The ordering-table entry: the average depth's OTZ, or the farthest or nearest corner's depth / 4, shifted by the table's shift. */
		uint16 BucketOf(const FCorners& Corners, EFieldDrawMode Mode, const FGte& Gte, int32 OrderingShift, bool& bZeroDepth)
		{
			const auto Depth = [&Corners](int32 Corner) { return Corners.At[static_cast<std::size_t>(Corner)]->ScreenZ; };
			if (Mode == EFieldDrawMode::Average || Mode == EFieldDrawMode::Relit)
			{
				const uint16 Average = Corners.Count == 4 ? Gte.AverageDepth4(Depth(0), Depth(1), Depth(2), Depth(3))
					: Gte.AverageDepth3(Depth(0), Depth(1), Depth(2));
				bZeroDepth = Average == 0;
				return static_cast<uint16>(Average >> OrderingShift);
			}
			uint16 Farthest = 0;
			uint16 Nearest = 0xFFFF;
			bZeroDepth = false;
			for (int32 Corner = 0; Corner < Corners.Count; ++Corner)
			{
				Farthest = std::max(Farthest, Depth(Corner));
				Nearest = std::min(Nearest, Depth(Corner));
				// A quad is dropped for any corner at zero depth, a triangle only when the corner it sorts by is.
				bZeroDepth = bZeroDepth || (Corners.Count == 4 && Depth(Corner) == 0);
			}
			const uint16 Sorted = Mode == EFieldDrawMode::Farthest ? Farthest : Nearest;
			bZeroDepth = bZeroDepth || Sorted == 0;
			return static_cast<uint16>(Sorted >> (OrderingShift + 2));
		}

		/**
		 * The original's per-polygon rules, the model's culling aside. Its handlers read the GTE's
		 * leading-zero count where they meant its flag, which never has the sign bit: a corner the GTE
		 * cannot project leaves nothing out, and is drawn where the division left it.
		 */
		uint8 OriginalDrops(const FCorners& Corners, uint16 ClipRows, uint16 ClipColumns)
		{
			uint8 Dropped = 0;
			bool bAcross = false;
			bool bDown = false;
			for (int32 Corner = 0; Corner < Corners.Count; ++Corner)
			{
				const FGteProjection& At = *Corners.At[static_cast<std::size_t>(Corner)];
				bAcross = bAcross || static_cast<uint16>(At.ScreenX) < ClipColumns;
				bDown = bDown || static_cast<uint16>(At.ScreenY) < ClipRows;
			}
			if (!bAcross || !bDown)
			{
				Dropped |= FieldPolygonDrop::OffScreen;
			}
			const FGteProjection& A = *Corners.At[0];
			const FGteProjection& B = *Corners.At[1];
			const FGteProjection& C = *Corners.At[2];
			if (Nclip(A.ScreenX, A.ScreenY, B.ScreenX, B.ScreenY, C.ScreenX, C.ScreenY) <= 0)
			{
				Dropped |= FieldPolygonDrop::BackFace;
			}
			return Dropped;
		}

		/** Every corner left of the screen, right of it, above it or below it. */
		bool IsBeyondAnEdge(const FCorners& Corners, uint16 ClipRows, uint16 ClipColumns)
		{
			bool bLeft = true;
			bool bRight = true;
			bool bAbove = true;
			bool bBelow = true;
			for (int32 Corner = 0; Corner < Corners.Count; ++Corner)
			{
				const FGteProjection& At = *Corners.At[static_cast<std::size_t>(Corner)];
				bLeft = bLeft && At.ScreenX < 0;
				bRight = bRight && At.ScreenX >= ClipColumns;
				bAbove = bAbove && At.ScreenY < 0;
				bBelow = bBelow && At.ScreenY >= ClipRows;
			}
			return bLeft || bRight || bAbove || bBelow;
		}

		void Sort(const FFieldScene& Scene, FFieldSceneFrame& Frame)
		{
			const int32 Far = std::min<int32>(Scene.State.FarBucket, EntriesPerTable - 1);
			Frame.BucketEnds.assign(2 * EntriesPerTable, 0);
			const auto Slot = [&Scene, &Frame](std::size_t Polygon)
			{
				const bool bBackground = Scene.Objects[Scene.Polygons[Polygon].Object].bBackground;
				return (bBackground ? 0 : EntriesPerTable) + Frame.Polygons[Polygon].Bucket;
			};
			for (std::size_t Polygon = 0; Polygon < Frame.Polygons.size(); ++Polygon)
			{
				if (Frame.Polygons[Polygon].bDrawn)
				{
					++Frame.BucketEnds[Slot(Polygon)];
				}
			}
			uint32 Next = 0;
			for (std::size_t Table = 0; Table < 2; ++Table)
			{
				for (int32 Bucket = Far; Bucket >= 0; --Bucket)
				{
					uint32& Entry = Frame.BucketEnds[Table * EntriesPerTable + static_cast<std::size_t>(Bucket)];
					const uint32 Count = Entry;
					Entry = Next;
					Next += Count;
				}
			}
			Frame.DrawOrder.resize(Next);
			// Each new primitive is linked at its entry's head: within an entry the last is drawn first.
			for (std::size_t Polygon = Frame.Polygons.size(); Polygon-- > 0;)
			{
				if (Frame.Polygons[Polygon].bDrawn)
				{
					Frame.DrawOrder[Frame.BucketEnds[Slot(Polygon)]++] = static_cast<uint32>(Polygon);
				}
			}
		}

		void TrackUntexturedBlend(const FFieldScene& Scene, FFieldSceneFrame& Frame)
		{
			EBlendMode Current = EBlendMode::Average;
			for (const uint32 Index : Frame.DrawOrder)
			{
				FFieldPolygonView& Polygon = Frame.Polygons[Index];
				const FFieldScenePolygon& Source = Scene.Polygons[Index];
				if (Scene.PrimitiveOf(Source).Traits().bTextured)
				{
					Current = Source.Page.Blend;
				}
				Polygon.UntexturedBlend = Current;
			}
		}
	}

	FFieldPolygonTest TestFieldPolygon(const FMdlPrimitive& Primitive, const FGteProjection* Projected, EFieldDrawMode Mode, const FGte& Gte, int32 OrderingShift,
		uint16 ClipRows, uint16 ClipColumns)
	{
		const FCorners Corners = CornersOf(Primitive, Projected);
		FFieldPolygonTest Test;
		bool bZeroDepth = false;
		Test.Bucket = BucketOf(Corners, Mode, Gte, OrderingShift, bZeroDepth);
		// The relighting mode's flat triangle alone is linked whatever its depth: at none, into the table's nearest entry.
		if (Mode == EFieldDrawMode::Relit && Primitive.Type == EMdlPrimitiveType::FlatTriangle)
		{
			bZeroDepth = false;
		}
		Test.Dropped = static_cast<uint8>(OriginalDrops(Corners, ClipRows, ClipColumns) | (bZeroDepth ? FieldPolygonDrop::ZeroDepth : 0));
		Test.bInRangeAnyFacing = (Test.Dropped & FieldPolygonDrop::OffScreen) == 0 || IsBeyondAnEdge(Corners, ClipRows, ClipColumns);
		Test.bInRange = WindsClockwiseFromEye(*Corners.At[0], *Corners.At[1], *Corners.At[2]) && Test.bInRangeAnyFacing;
		return Test;
	}

	FGte FieldDrawingGte(const FFieldView& View)
	{
		FGte Gte;
		Gte.OffsetX = FieldScreenCenterX << 16;
		Gte.OffsetY = FieldScreenCenterY << 16;
		Gte.ProjectionDistance = View.ProjectionDistance;
		Gte.AverageScale3 = 0x155;
		Gte.AverageScale4 = 0x100;
		Gte.DepthCueA = -0x1062;
		Gte.DepthCueB = 0x1400000;
		return Gte;
	}

	FGteMatrix FieldObjectMatrix(const FFieldSceneObject& Object, const FFieldView& View)
	{
		if (Object.bWithoutWorldRotation)
		{
			// The view with what carries the model first, then the model's own: the order the rounding follows.
			return CompMatrix(CompMatrix(View.View, Object.Placement), Object.Attached);
		}
		FGteMatrix Matrix;
		const FGteVector Position = {static_cast<int16>(Object.Placement.T[0]), static_cast<int16>(Object.Placement.T[1]), static_cast<int16>(Object.Placement.T[2])};
		const FGteMatrix& Under = View.Scene;
		Matrix.T = RotTrans(Under, Position);
		if (Object.MatrixMode == EFieldMatrixMode::FacingScreen)
		{
			Matrix.M = MulMatrix(View.ScreenRotation, MulMatrix(View.Billboard, Object.Placement)).M;
		}
		else
		{
			Matrix.M = MulMatrix(Under, Object.Placement).M;
		}
		return Matrix;
	}

	FAffine FieldObjectAffine(const FFieldSceneObject& Object, const FFieldView& View)
	{
		// A matrix composed after another counts the low 16 bits of its translation, as the GTE's composition reads them.
		const auto Placed = [](const FGteMatrix& Matrix)
		{
			FAffine Result = FAffine::From(Matrix);
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Result.T[Axis] = static_cast<float>(static_cast<int16>(Matrix.T[Axis]));
			}
			return Result;
		};
		if (Object.bWithoutWorldRotation)
		{
			return FAffine::From(View.View).After(Placed(Object.Placement)).After(Placed(Object.Attached));
		}
		FAffine Matrix = FAffine::From(View.Scene).After(Placed(Object.Placement));
		if (Object.MatrixMode == EFieldMatrixMode::FacingScreen)
		{
			Matrix.M = FAffine::From(View.ScreenRotation).After(FAffine::From(View.Billboard)).After(Placed(Object.Placement)).M;
		}
		return Matrix;
	}

	bool IsFieldObjectOffScreen(const FFieldSceneObject& Object, const FGteMatrix& Matrix, const FFieldRenderState& State, const FGte& Drawing)
	{
		// The bounds' center in view space, then a square of the radius around it, facing the screen.
		FGte Gte = Drawing;
		Gte.Transform = RotationXyz({0, 0, 0});
		Gte.Transform.T = RotTrans(Matrix, Object.Bounds.Centre);
		const int16 Radius = Object.Bounds.Radius;
		const FGteProjection Low = Gte.Rtps({static_cast<int16>(-Radius), static_cast<int16>(-Radius), 0});
		const FGteProjection High = Gte.Rtps({Radius, Radius, 0});
		const bool bOnScreen = Low.ScreenY < State.CullMarginY + CullHeight && -State.CullMarginY < High.ScreenY
			&& Low.ScreenX < State.CullMarginX + CullWidth && -State.CullMarginX < High.ScreenX;
		return !bOnScreen;
	}

	void EvaluateFieldScene(const FFieldScene& Scene, const FFieldView& View, EFieldCulling Culling, FFieldSceneFrame& Frame)
	{
		FGte Drawing = FieldDrawingGte(View);
		const FFieldFog& Fog = Scene.State.Fog;
		if (Fog.bOn)
		{
			// As the field sets the GTE up before its models when fog is on; a range the library refuses leaves the field's own.
			Drawing.FarColor = {Fog.FarColor[0] << 4, Fog.FarColor[1] << 4, Fog.FarColor[2] << 4};
			Drawing.SetFogNearFar(Fog.Near, Fog.Far, View.ProjectionDistance);
		}
		Frame.Objects.resize(Scene.Objects.size());
		Frame.Polygons.resize(Scene.Polygons.size());
		std::size_t CornerCount = 0;
		for (const FFieldSceneObject& Object : Scene.Objects)
		{
			CornerCount += Scene.PartOf(Object).Vertices.size();
		}
		Frame.Corners.resize(CornerCount);
		std::size_t FirstCorner = 0;
		for (std::size_t Number = 0; Number < Scene.Objects.size(); ++Number)
		{
			const FFieldSceneObject& Object = Scene.Objects[Number];
			const FMdlPart& Part = Scene.PartOf(Object);
			FFieldObjectView& Placed = Frame.Objects[Number];
			Placed.Matrix = FieldObjectMatrix(Object, View);
			Placed.Exact = FieldObjectAffine(Object, View);
			Placed.bCulled = Object.bHidden || (!Object.bNeverCulled && IsFieldObjectOffScreen(Object, Placed.Matrix, Scene.State, Drawing));
			Placed.bAsBuilt = Object.IsAsBuilt();
			FGte Gte = Drawing;
			Gte.Transform = Placed.Matrix;
			if (Object.DrawMode == EFieldDrawMode::Relit)
			{
				Scene.Lights.Load(Gte, ScaleColumns(Placed.Matrix, {LightScale, LightScale, LightScale}));
			}
			const std::span<const FGteVector> Vertices = Object.MovedVertices.size() == Part.Vertices.size() ? Object.MovedVertices
																											  : std::span<const FGteVector>(Part.Vertices);
			FGteProjection* Projected = &Frame.Corners[FirstCorner];
			for (std::size_t Vertex = 0; Vertex < Vertices.size(); ++Vertex)
			{
				Projected[Vertex] = Gte.Rtps(Vertices[Vertex]);
			}
			FirstCorner += Part.Vertices.size();
			for (uint32 Index = Object.FirstPolygon; Index < Object.FirstPolygon + Object.PolygonCount; ++Index)
			{
				const FFieldScenePolygon& Source = Scene.Polygons[Index];
				const FMdlPrimitive& Primitive = Part.Primitives[Source.Primitive];
				FFieldPolygonView& Polygon = Frame.Polygons[Index];
				for (int32 Corner = 0; Corner < Primitive.Traits().Corners; ++Corner)
				{
					const FGteProjection& At = Projected[Primitive.Vertices[static_cast<std::size_t>(Corner)]];
					Polygon.ScreenX[static_cast<std::size_t>(Corner)] = At.ScreenX;
					Polygon.ScreenY[static_cast<std::size_t>(Corner)] = At.ScreenY;
				}
				// In fog only FT3 and FT4 have handlers of their own; every other kind draws as draw mode 0 does.
				const bool bFogged = Object.bFogged && IsFlatTextured(Primitive);
				const EFieldDrawMode SortMode = Object.bFogged && !bFogged ? EFieldDrawMode::Average : Object.DrawMode;
				const FFieldPolygonTest Test = TestFieldPolygon(Primitive, Projected, SortMode, Gte);
				Polygon.Bucket = Test.Bucket;
				Polygon.Dropped = Test.Dropped;
				if (Placed.bCulled)
				{
					Polygon.Dropped |= FieldPolygonDrop::Model;
				}
				const bool bBeyond = Polygon.Bucket > Scene.State.FarBucket;
				if (bBeyond)
				{
					Polygon.Dropped |= FieldPolygonDrop::BeyondFarBucket;
				}
				Polygon.bDrawn = Culling == EFieldCulling::Original ? Polygon.Dropped == 0 : !Object.bHidden && !bBeyond && Test.bInRangeAnyFacing;
				Polygon.Colors = Source.Packet.Colors;
				Polygon.bFogged = bFogged;
				if (bFogged)
				{
					// The fog's base colour depth-cued by the last corner projected, which then modulates the texels.
					const FGteProjection& Last = Projected[Primitive.Vertices[static_cast<std::size_t>(Primitive.Traits().Corners - 1)]];
					const FGteColor Faded = Gte.DepthCueColor({Fog.Color[0], Fog.Color[1], Fog.Color[2], 0}, Last.DepthCue);
					Polygon.Colors.fill({Faded.R, Faded.G, Faded.B});
				}
				else if (Object.DrawMode == EFieldDrawMode::Relit && Source.Packet.Relight != EMdlRelight::None)
				{
					FMdlPacket Lit = Source.Packet;
					RelightMdlPacket(Lit, Primitive, Part, Gte);
					Polygon.Colors = Lit.Colors;
				}
			}
		}
		Sort(Scene, Frame);
		TrackUntexturedBlend(Scene, Frame);
	}
}
