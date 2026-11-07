#include "FieldGearDraw.h"

#include "FieldDrawBuilder.h"
#include "Field/FieldGearDrawing.h"
#include "Gears/GearPackets.h"
#include "Rendering/VramTexture.h"

#include <algorithm>

namespace
{
	/** A four-point polygon as two triangles, both wound as the first. */
	constexpr int32 GearQuadTriangles[2][3] = {{0, 1, 2}, {1, 3, 2}};
	constexpr uint8 SemiTransparentBlend = 1;
	constexpr uint8 TexturedBlend = 2;
	constexpr uint8 PageBlendShift = 2;
	/** The floor shadow's, which the depth rule draws as the characters' shadows. */
	constexpr uint8 ShadowBlend = 0x10;
	/** The GPU code's bits: raw texture, semi-transparent. */
	constexpr uint8 GearRawCode = 1;
	constexpr uint8 GearSemiTransparentCode = 2;

	uint32 PackGearColor(const Weltall::FMdlColor& Color)
	{
		return static_cast<uint32>(Color.R | Color.G << 8 | Color.B << 16);
	}

	uint32 PackGearTexel(uint32 U, uint32 V)
	{
		return U | V << 8;
	}

	/** A polygon's texel bounds, the corners' smallest and largest coordinates. */
	void SetGearTexels(FFieldScenePolygonState& State, std::span<const Weltall::FMdlUv> Uvs)
	{
		uint32 LowU = 255, LowV = 255, HighU = 0, HighV = 0;
		for (const Weltall::FMdlUv& Uv : Uvs)
		{
			LowU = FMath::Min<uint32>(LowU, Uv.U);
			LowV = FMath::Min<uint32>(LowV, Uv.V);
			HighU = FMath::Max<uint32>(HighU, Uv.U);
			HighV = FMath::Max<uint32>(HighV, Uv.V);
		}
		State.TexelMin = PackGearTexel(LowU, LowV);
		State.TexelMax = PackGearTexel(HighU, HighV);
	}

	uint8 GearBlendOf(uint8 Code, bool bTextured, uint16 TexturePage)
	{
		const Weltall::EBlendMode Page = Weltall::FTexturePage::FromAttribute(TexturePage).Blend;
		return static_cast<uint8>(((Code & GearSemiTransparentCode) != 0 ? SemiTransparentBlend : 0) | (bTextured ? TexturedBlend : 0)
			| static_cast<uint8>(Page) << PageBlendShift);
	}
}

void FFieldSceneMesh::AddGears(std::span<const Weltall::FFieldGearLayoutItem> Gears)
{
	for (uint32 Object = 0; Object < Gears.size(); ++Object)
	{
		const Weltall::FFieldGearLayoutItem& Item = Gears[Object];
		const uint32 Row = GearObjectRow + Object;
		const auto AddCorner = [this, Row](const Weltall::FGteVector& At, const Weltall::FMdlUv& Uv, uint32 Corner)
		{
			FFieldSceneCorner& Out = CornerData.AddDefaulted_GetRef();
			Out.Position = FVector3f(At[0], At[1], At[2]);
			Out.TexCoord = FVector2f(Uv.U, Uv.V);
			Out.Polygon = static_cast<uint32>(PolygonData.Num());
			Out.Object = Row;
			Out.Corner = Corner;
		};
		if (Item.Packets == nullptr)
		{
			// The floor shadow: the sprites' blob on a quad of the model's half-sizes, on its floor.
			using namespace Weltall::FieldGearShadow;
			const int16 X = Item.HalfWidth;
			const int16 Z = Item.HalfDepth;
			const Weltall::FGteVector Corners[4] = {{X, 0, Z}, {static_cast<int16>(-X), 0, Z}, {X, 0, static_cast<int16>(-Z)},
				{static_cast<int16>(-X), 0, static_cast<int16>(-Z)}};
			const Weltall::FMdlUv Uvs[4] = {{FirstTexel, FirstRow}, {LastTexel, FirstRow}, {FirstTexel, LastRow}, {LastTexel, LastRow}};
			PolygonCorners.Add(static_cast<uint32>(CornerData.Num()));
			for (const auto& Triangle : GearQuadTriangles)
			{
				for (const int32 Corner : Triangle)
				{
					AddCorner(Corners[Corner], Uvs[Corner], 0);
				}
			}
			FFieldScenePolygonState& State = PolygonData.AddDefaulted_GetRef();
			State.Texture = FVramTexture::Pack(Weltall::FTexturePage::FromAttribute(TexturePage), Weltall::FClutPosition::FromAttribute(Clut));
			State.Flags = FieldPolygonFlags::BroadShadow;
			SetGearTexels(State, Uvs);
			GearBlends.Add(static_cast<uint8>(GearBlendOf(Code, true, TexturePage) | ShadowBlend));
			continue;
		}
		const Weltall::FMdlPart& Part = *Item.Part;
		for (uint32 Number = 0; Number < Item.PolygonCount; ++Number)
		{
			const Weltall::FMdlPrimitive& Primitive = Part.Primitives[Number];
			const Weltall::FMdlPrimitiveTraits& Traits = Primitive.Traits();
			const uint8 Code = Item.Packets->Packets[Number].Code;
			const Weltall::FMdlTextureState& Texture = Item.Packets->Textures[Number];
			PolygonCorners.Add(static_cast<uint32>(CornerData.Num()));
			for (int32 Triangle = 0; Triangle < (Traits.Corners == 4 ? 2 : 1); ++Triangle)
			{
				for (const int32 Corner : GearQuadTriangles[Triangle])
				{
					const std::size_t At = static_cast<std::size_t>(Corner);
					AddCorner(Part.Vertices[Primitive.Vertices[At]], Primitive.Uvs[At], Traits.bGouraud ? static_cast<uint32>(Corner) : 0);
				}
			}
			FFieldScenePolygonState& State = PolygonData.AddDefaulted_GetRef();
			State.Texture = FVramTexture::NoTexture;
			if (Traits.bTextured)
			{
				State.Texture = Item.bPortRows
					? FVramTexture::Pack(Weltall::FTexturePage::FromPortAttribute(Texture.TexturePage), Weltall::FClutPosition::FromPortAttribute(Texture.Clut))
					: FVramTexture::Pack(Weltall::FTexturePage::FromAttribute(Texture.TexturePage), Weltall::FClutPosition::FromAttribute(Texture.Clut));
				State.Flags = (Code & GearRawCode) != 0 ? FieldPolygonFlags::Raw : 0;
				SetGearTexels(State, std::span<const Weltall::FMdlUv>(Primitive.Uvs).first(Traits.Corners));
			}
			// What faces away is left out as each frame is drawn (FFieldPolygonTest::bInRangeAnyFacing).
			State.Flags |= FieldPolygonFlags::OneSided;
			GearBlends.Add(GearBlendOf(Code, Traits.bTextured, Texture.TexturePage));
		}
	}
}

FFieldDrawBuilder::FItem FFieldDrawBuilder::DescribeGear(const Weltall::FFieldDrawItem& Item) const
{
	FItem Described;
	if (!Draw.bGearsDrawn || Gears == nullptr)
	{
		return Described;
	}
	if (Item.Gear == Weltall::EFieldGearDraw::Cloth)
	{
		const uint16 Page = Gears->Cloths[Item.Index].TexturePage;
		Described.FirstCorner = Draw.FirstClothCorner + 3 * Item.Index;
		Described.CornerCount = 3;
		Described.bTextured = true;
		Described.bModel = true;
		Described.PageBlend = Weltall::FTexturePage::FromAttribute(Page).Blend;
		return Described;
	}
	const uint32 Polygon = Mesh.FirstGearPolygon() + Item.Index;
	const uint8 Blend = Mesh.GearBlend(Item.Index);
	Described.FirstCorner = Mesh.FirstCorner(Polygon);
	Described.CornerCount = Mesh.CornerCount(Polygon);
	Described.bSemiTransparent = (Blend & SemiTransparentBlend) != 0;
	Described.bTextured = (Blend & TexturedBlend) != 0;
	Described.PageBlend = static_cast<Weltall::EBlendMode>(Blend >> PageBlendShift & 3);
	Described.bShadow = (Blend & ShadowBlend) != 0;
	Described.bModel = !Described.bShadow;
	return Described;
}

bool DoGearsFitMesh(const Weltall::FFieldFrame& Latest, const FFieldSceneMesh& Mesh)
{
	return Latest.Gears.Layout == Mesh.GearLayout() && Latest.Gears.Polygons.size() == Mesh.GearPolygons();
}

void AddGearColors(const Weltall::FFieldFrame& Latest, const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw)
{
	if (!Draw.bGearsDrawn)
	{
		Draw.Colors.AddZeroed(static_cast<int32>(Mesh.GearPolygons()));
		return;
	}
	for (const Weltall::FFieldGearPolygon& Polygon : Latest.Gears.Polygons)
	{
		Draw.Colors.Emplace(PackGearColor(Polygon.Colors[0]), PackGearColor(Polygon.Colors[1]), PackGearColor(Polygon.Colors[2]), PackGearColor(Polygon.Colors[3]));
	}
}

void AddGearObjects(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Before, float Alpha, FFieldSceneDraw& Draw)
{
	if (!Draw.bGearsDrawn)
	{
		return;
	}
	const std::vector<Weltall::FFieldGearObject>& Objects = Latest.Gears.Objects;
	const bool bPairs = Before != nullptr && Before->Gears.Layout == Latest.Gears.Layout && Before->Gears.Objects.size() == Objects.size();
	for (std::size_t Object = 0; Object < Objects.size(); ++Object)
	{
		const Weltall::FFieldGearObject& Now = Objects[Object];
		const Weltall::FFieldGearObject* Then = bPairs ? &Before->Gears.Objects[Object] : nullptr;
		const bool bBlends = Then != nullptr && Then->bDrawn && Then->bFine == Now.bFine && IsNearInWorld(Then->World, Now.World, FieldJumpDistance);
		if (Now.bFine)
		{
			AddBlendedFieldObject(Draw.Objects, bBlends ? Then->Fine : Now.Fine, Now.Fine, bBlends ? Alpha : 1.0f);
			continue;
		}
		AddBlendedFieldObject(Draw.Objects, bBlends ? Then->Matrix : Now.Matrix, Now.Matrix, bBlends ? Alpha : 1.0f);
	}
	// The cloths lie in the world: drawn by the view.
	AddBlendedFieldObject(Draw.Objects, Before != nullptr ? Before->View.View : Latest.View.View, Latest.View.View, Before != nullptr ? Alpha : 1.0f);
	// A placed model's cloths lie in its own space: a row each, after the view's.
	for (const Weltall::FFieldGearClothSpace& Space : Latest.Gears.ClothSpaces)
	{
		const Weltall::FFieldGearClothSpace* Then = nullptr;
		if (bPairs)
		{
			const auto Found = std::find_if(Before->Gears.ClothSpaces.begin(), Before->Gears.ClothSpaces.end(),
				[&Space](const Weltall::FFieldGearClothSpace& Earlier) { return Earlier.Slot == Space.Slot; });
			Then = Found != Before->Gears.ClothSpaces.end() ? &*Found : nullptr;
		}
		AddBlendedFieldObject(Draw.Objects, Then != nullptr ? Then->Space : Space.Space, Space.Space, Then != nullptr ? Alpha : 1.0f);
	}
}

void AddGearCloths(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Before, float Alpha, const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw)
{
	Draw.FirstClothCorner = Mesh.Corners() + static_cast<uint32>(Draw.DynamicCorners.Num());
	if (!Draw.bGearsDrawn)
	{
		return;
	}
	const uint32 ViewRow = Mesh.FirstGearObject() + static_cast<uint32>(Latest.Gears.Objects.size());
	const bool bPairs = Before != nullptr && Before->Gears.Layout == Latest.Gears.Layout;
	const std::vector<Weltall::FFieldGearClothSpace>& Spaces = Latest.Gears.ClothSpaces;
	for (const Weltall::FGearDrawnClothTriangle& Triangle : Latest.Gears.Cloths)
	{
		const auto Space = std::find_if(Spaces.begin(), Spaces.end(), [&Triangle](const Weltall::FFieldGearClothSpace& Placed) { return Placed.Slot == Triangle.Slot; });
		const uint32 Row = Space != Spaces.end() ? ViewRow + 1 + static_cast<uint32>(Space - Spaces.begin()) : ViewRow;
		const uint32 Polygon = Mesh.Polygons() + static_cast<uint32>(Draw.DynamicPolygons.Num());
		for (std::size_t Corner = 0; Corner < 3; ++Corner)
		{
			const Weltall::FGteVector& Now = Triangle.Points[Corner];
			FVector3f At(Now[0], Now[1], Now[2]);
			if (bPairs)
			{
				// The same point, wherever the tick before drew it.
				for (const Weltall::FGearDrawnClothTriangle& Earlier : Before->Gears.Cloths)
				{
					const auto Found = std::find(Earlier.PointKeys.begin(), Earlier.PointKeys.end(), Triangle.PointKeys[Corner]);
					if (Found != Earlier.PointKeys.end())
					{
						const Weltall::FGteVector& Then = Earlier.Points[static_cast<std::size_t>(Found - Earlier.PointKeys.begin())];
						At = FMath::Lerp(FVector3f(Then[0], Then[1], Then[2]), At, Alpha);
						break;
					}
				}
			}
			FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
			Out.Position = At;
			Out.TexCoord = FVector2f(Triangle.Uvs[Corner].U, Triangle.Uvs[Corner].V);
			Out.Polygon = Polygon;
			Out.Object = Row;
			Out.Corner = static_cast<uint32>(Corner);
		}
		FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
		// A cloth's page and CLUT are made from its place: their high y bits are set only for one in the port's rows.
		State.Texture = FVramTexture::Pack(Weltall::FTexturePage::FromPortAttribute(Triangle.TexturePage), Weltall::FClutPosition::FromPortAttribute(Triangle.Clut));
		SetGearTexels(State, Triangle.Uvs);
		Draw.Colors.Emplace(PackGearColor(Triangle.Colors[0]), PackGearColor(Triangle.Colors[1]), PackGearColor(Triangle.Colors[2]), 0u);
	}
}
