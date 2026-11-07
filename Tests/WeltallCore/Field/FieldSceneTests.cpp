#include "Field/FieldScene.h"
#include "Field/FieldSceneFrame.h"
#include "Graphics/GteVectors.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

namespace
{
	constexpr uint8 RawTexturedTriangle = 0x25;
	constexpr uint8 FlatSemiTransparent = 0x22;

	/**
	 * Triangles of one model in view space as the test views it: every model vertex is moved 1000
	 * units ahead of the eye, and H is 500, so (x, y, 0) lands on screen at (160 + x / 2, 112 + y / 2).
	 */
	struct FTestScene
	{
		FFieldScene Scene;
		FFieldView View;

		FTestScene()
		{
			Scene.Models.emplace_back().Parts.emplace_back();
			View.View = FGteMatrix::Identity();
			View.View.T = {0, 0, 1000};
			View.Scene = View.View;
			View.ScreenRotation = FGteMatrix::Identity();
			View.Billboard = FGteMatrix::Identity();
			View.ProjectionDistance = 500;
		}

		FMdlPart& Part() { return Scene.Models.front().Parts.front(); }

		/** Adds a triangle and returns its polygon number; the object's polygons must be added last to first object. */
		uint32 Triangle(const FGteVector& A, const FGteVector& B, const FGteVector& C, uint8 Code = RawTexturedTriangle, uint16 Page = 0)
		{
			const uint16 First = static_cast<uint16>(Part().Vertices.size());
			Part().Vertices.insert(Part().Vertices.end(), {A, B, C});
			FMdlPrimitive Primitive;
			Primitive.Type = (Code & 4) != 0 ? EMdlPrimitiveType::StaticTexturedTriangle : EMdlPrimitiveType::StaticFlatTriangle;
			Primitive.Code = Code;
			Primitive.Vertices = {First, static_cast<uint16>(First + 1), static_cast<uint16>(First + 2), 0};
			Part().Primitives.push_back(Primitive);
			FFieldScenePolygon Polygon;
			Polygon.Primitive = static_cast<uint16>(Part().Primitives.size() - 1);
			Polygon.Packet.Code = Code;
			Polygon.Page = FTexturePage::FromAttribute(Page);
			Scene.Polygons.push_back(Polygon);
			return static_cast<uint32>(Scene.Polygons.size() - 1);
		}

		/** Places the model once more as an object owning the polygons from First on. */
		FFieldSceneObject& Object(uint32 First, bool bBackground = false)
		{
			FFieldSceneObject& Object = Scene.Objects.emplace_back();
			Object.Placement = RotationXyz({0, 0, 0});
			Object.Bounds = {{0, 0, 0}, 2000};
			Object.bBackground = bBackground;
			Object.FirstPolygon = First;
			Object.PolygonCount = static_cast<uint32>(Scene.Polygons.size()) - First;
			for (uint32 Index = First; Index < Scene.Polygons.size(); ++Index)
			{
				Scene.Polygons[Index].Object = static_cast<uint16>(Scene.Objects.size() - 1);
			}
			return Object;
		}

		FFieldSceneFrame Evaluate(EFieldCulling Culling = EFieldCulling::Original) const
		{
			FFieldSceneFrame Frame;
			EvaluateFieldScene(Scene, View, Culling, Frame);
			return Frame;
		}
	};
}

TEST_CASE("The field's scene frame keeps the original's rules for each polygon", "[Field]")
{
	FTestScene Test;
	const uint32 Facing = Test.Triangle({0, 0, 0}, {100, 0, 0}, {0, 100, 0});
	const uint32 Away = Test.Triangle({0, 0, 0}, {0, 100, 0}, {100, 0, 0});
	const uint32 Right = Test.Triangle({400, 0, 0}, {500, 0, 0}, {400, 100, 0});
	const uint32 Below = Test.Triangle({0, 300, 0}, {100, 300, 0}, {0, 400, 0});
	const uint32 Near = Test.Triangle({0, 0, -800}, {100, 0, 0}, {0, 100, 0});
	const uint32 Across = Test.Triangle({-400, 0, 0}, {400, 0, 0}, {-400, 100, 0});
	Test.Object(0);
	// 50000 units ahead: OTZ 12737, entry 3184, past the far bucket 0x720.
	const uint32 Far = Test.Triangle({0, 0, 25000}, {3000, 0, 25000}, {0, 3000, 25000});
	Test.Object(Far).Placement.T = {0, 0, 25000};
	const FFieldSceneFrame Frame = Test.Evaluate();
	const FFieldPolygonView& Shown = Frame.Polygons[Facing];
	CHECK(Shown.ScreenX == std::array<int16, 4>{160, 210, 160, 0});
	CHECK(Shown.ScreenY == std::array<int16, 4>{112, 112, 162, 0});
	// OTZ = 0x155 * 3000 >> 12 = 249: entry 62.
	CHECK(Shown.Bucket == 62);
	CHECK(Shown.Dropped == 0);
	CHECK(Frame.Polygons[Away].Dropped == FieldPolygonDrop::BackFace);
	CHECK(Frame.Polygons[Right].Dropped == FieldPolygonDrop::OffScreen);
	CHECK(Frame.Polygons[Below].Dropped == FieldPolygonDrop::OffScreen);
	CHECK(Frame.Polygons[Across].Dropped == FieldPolygonDrop::OffScreen);
	CHECK(Frame.Polygons[Far].Dropped == FieldPolygonDrop::BeyondFarBucket);
	// Nearer than H / 2 the GTE's division overflows: the handlers read the leading-zero count for its
	// flag and draw the polygon anyway, its corner where the division left it.
	const FFieldPolygonView& Stretched = Frame.Polygons[Near];
	CHECK(Stretched.Dropped == 0);
	// The near corner lies on the eye's axis: the saturated division leaves it at the screen's centre.
	CHECK(Stretched.ScreenX[0] == 160);
	CHECK(Stretched.ScreenY[0] == 112);
	CHECK(Frame.DrawOrder == std::vector<uint32>{Facing, Near});

	// Everything in range for a wider view: the one too far and the one across the screen stay out. The one facing away
	// stays in, for the drawing tests its facing in every frame against the view it draws with (a free camera turns
	// between ticks).
	const FFieldSceneFrame Wide = Test.Evaluate(EFieldCulling::InRange);
	CHECK(Wide.DrawOrder.size() == 5);
	CHECK(Wide.Polygons[Away].bDrawn);
	CHECK_FALSE(Wide.Polygons[Far].bDrawn);
	CHECK(Wide.Polygons[Right].bDrawn);
	CHECK(Wide.Polygons[Near].bDrawn);
	// Across the screen with no corner on it, it is no wider view's side but what the original never shows.
	CHECK_FALSE(Wide.Polygons[Across].bDrawn);
}

TEST_CASE("The field's scene frame draws the background table first, far buckets first, the last linked first", "[Field]")
{
	FTestScene Test;
	const uint32 Nearer = Test.Triangle({0, 0, -500}, {50, 0, -500}, {0, 50, -500});
	const uint32 First = Test.Triangle({0, 0, 0}, {100, 0, 0}, {0, 100, 0});
	const uint32 Second = Test.Triangle({10, 0, 0}, {100, 0, 0}, {10, 100, 0});
	Test.Object(0);
	const uint32 Sky = Test.Triangle({0, 0, -600}, {50, 0, -600}, {0, 50, -600});
	Test.Object(Sky, true);
	const FFieldSceneFrame Frame = Test.Evaluate();
	CHECK(Frame.DrawOrder == std::vector<uint32>{Sky, Second, First, Nearer});

	Test.Scene.Objects[0].DrawMode = EFieldDrawMode::Farthest;
	Test.Scene.State.FarBucket = 40;
	const FFieldSceneFrame Clipped = Test.Evaluate();
	// The farthest corner / 16: 1000 / 16 = 62, past the far bucket; 500 / 16 = 31 is not.
	CHECK(Clipped.Polygons[First].Bucket == 62);
	CHECK(Clipped.DrawOrder == std::vector<uint32>{Sky, Nearer});
}

TEST_CASE("The field culls a model by its bounds on the 320 x 224 screen and the margins", "[Field]")
{
	FTestScene Test;
	Test.Triangle({0, 0, 0}, {100, 0, 0}, {0, 100, 0});
	FFieldSceneObject& Object = Test.Object(0);
	Object.Bounds = {{700, 0, 0}, 100};
	CHECK(Test.Evaluate().Objects[0].bCulled);
	CHECK(Test.Evaluate().Polygons[0].Dropped == FieldPolygonDrop::Model);
	// The square around the bounds' center spans 160 + (700 - 100) / 2 = 460 onward.
	Test.Scene.State.CullMarginX = 141;
	CHECK_FALSE(Test.Evaluate().Objects[0].bCulled);
	CHECK(Test.Evaluate(EFieldCulling::InRange).Polygons[0].bDrawn);
	Test.Scene.Objects[0].bHidden = true;
	CHECK_FALSE(Test.Evaluate(EFieldCulling::InRange).Polygons[0].bDrawn);
}

TEST_CASE("An untextured semi-transparent polygon blends by the page of the textured one drawn before it", "[Field]")
{
	FTestScene Test;
	const uint32 Flat = Test.Triangle({0, 0, 0}, {100, 0, 0}, {0, 100, 0}, FlatSemiTransparent);
	const uint32 Additive = Test.Triangle({0, 0, 100}, {100, 0, 100}, {0, 100, 100}, RawTexturedTriangle, 1 << 5);
	const uint32 Before = Test.Triangle({0, 0, 200}, {100, 0, 200}, {0, 100, 200}, FlatSemiTransparent);
	Test.Object(0);
	const FFieldSceneFrame Frame = Test.Evaluate();
	CHECK(Frame.DrawOrder == std::vector<uint32>{Before, Additive, Flat});
	CHECK(Frame.Polygons[Before].UntexturedBlend == EBlendMode::Average);
	CHECK(Frame.Polygons[Flat].UntexturedBlend == EBlendMode::Add);
}

TEST_CASE("A model kept facing the screen turns by the camera's pitch alone", "[Field]")
{
	FTestScene Test;
	Test.Triangle({0, 0, 0}, {100, 0, 0}, {0, 100, 0});
	FFieldSceneObject& Object = Test.Object(0);
	Object.Placement = RotationXyz({0, 1024, 0});
	Object.Placement.T = {10, 20, 30};
	Test.View.Scene.M[0][0] = 2 * 4096;
	const FGteMatrix Placed = FieldObjectMatrix(Object, Test.View);
	CHECK(Placed.M == MulMatrix(Test.View.Scene, Object.Placement).M);
	CHECK(Placed.T == std::array<int32, 3>{20, 20, 1030});
	Object.MatrixMode = EFieldMatrixMode::FacingScreen;
	Test.View.Billboard = RotationXyz({512, 0, 0});
	const FGteMatrix Facing = FieldObjectMatrix(Object, Test.View);
	CHECK(Facing.M == MulMatrix(Test.View.Billboard, Object.Placement).M);
	CHECK(Facing.T == Placed.T);
}
