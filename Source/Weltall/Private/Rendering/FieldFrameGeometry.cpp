#include "FieldDrawBuilder.h"

#include "Field/FieldScene.h"
#include "Rendering/VramTexture.h"

#include <algorithm>

namespace
{
	bool IsWithinJump(const std::array<int32, 3>& A, const std::array<int32, 3>& B)
	{
		for (std::size_t Axis = 0; Axis < 3; ++Axis)
		{
			if (FMath::Abs(A[Axis] - B[Axis]) > FieldJumpDistance)
			{
				return false;
			}
		}
		return true;
	}

	/** How far past either side of the original's screen the panorama goes on, for wider views. */
	constexpr int32 BackdropBeyond = Weltall::FieldBackdropWidth;
	/** The view depth the panorama is drawn at: past anything the ordering tables sort. */
	constexpr float BackdropDepth = 65536.0f;

	uint32 PackClampedTexel(int32 U, int32 V)
	{
		return static_cast<uint32>(FMath::Clamp(U, 0, 255)) | static_cast<uint32>(FMath::Clamp(V, 0, 255)) << 8;
	}

	/** The whole cell a character's quad shows some or all rows of. */
	Weltall::FTextureRegion SpriteCellOf(const Weltall::FSpriteQuad& Quad, uint32 View)
	{
		const bool bPart = Quad.CellV1 > Quad.CellV0;
		const auto Texel = [](int32 Value) { return static_cast<uint8>(FMath::Clamp(Value, 0, 255)); };
		return Weltall::TextureRegionBetween(View, Texel(FMath::Min(Quad.U0, Quad.U1)), Texel(bPart ? Quad.CellV0 : FMath::Min(Quad.V0, Quad.V1)),
			Texel(FMath::Max(Quad.U0, Quad.U1) - 1), Texel((bPart ? Quad.CellV1 : FMath::Max(Quad.V0, Quad.V1)) - 1));
	}

	uint32 PackRgb(uint8 R, uint8 G, uint8 B)
	{
		return static_cast<uint32>(R) | static_cast<uint32>(G) << 8 | static_cast<uint32>(B) << 16;
	}

	FVector3f ToVector(const std::array<float, 3>& Point)
	{
		return FVector3f(Point[0], Point[1], Point[2]);
	}

}

FVector3f FieldCharacterOffset(const Weltall::FFieldCharacterView& Character, const Weltall::FFieldFrame* Previous, float Alpha)
{
	if (Previous == nullptr)
	{
		return FVector3f::ZeroVector;
	}
	for (const Weltall::FFieldCharacterView& Before : Previous->Characters)
	{
		if (Before.BlendKey == Character.BlendKey)
		{
			if (!IsWithinJump(Before.World, Character.World))
			{
				return FVector3f::ZeroVector;
			}
			return (ToVector(Before.Origin) - ToVector(Character.Origin)) * (1.0f - Alpha);
		}
	}
	return FVector3f::ZeroVector;
}

namespace
{
	/** The particle of the tick before this one continues: the same effect's, emitter's and place's, one tick older. */
	const Weltall::FFieldParticleQuad* ParticleBefore(const Weltall::FFieldParticleQuad& Particle, const Weltall::FFieldFrame* Previous)
	{
		if (Previous == nullptr)
		{
			return nullptr;
		}
		for (const Weltall::FFieldParticleQuad& Before : Previous->Particles)
		{
			if (Before.BlendKey == Particle.BlendKey)
			{
				return Before.Life == Particle.Life + 1 && IsWithinJump(Before.World, Particle.World) ? &Before : nullptr;
			}
		}
		return nullptr;
	}

	/** Which of the scene's joins hold in a drawn frame: those of models standing as built in the tick drawn and in the one it is drawn from. */
	struct FJoinsHeld
	{
		const Weltall::FFieldFrame& Latest;
		const Weltall::FFieldFrame* Previous = nullptr;

		bool Stands(uint32 Object) const
		{
			return Latest.Scenery.Objects[Object].bAsBuilt && (Previous == nullptr || Previous->Scenery.Objects[Object].bAsBuilt);
		}

		bool Holds(const Weltall::FFieldSceneJoin& Join) const { return Stands(Join.Object) && Stands(Join.Anchor); }
	};

	/** The joins as a frame drawn Alpha of the way from Previous holds them; the tick before counts only where it is the same scene's. */
	FJoinsHeld JoinsHeld(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous)
	{
		return {Latest, Previous != nullptr && Previous->Scenery.Objects.size() == Latest.Scenery.Objects.size() ? Previous : nullptr};
	}

	/**
	 * A copy of the mesh's polygon as dynamic corners: its model's vertices as VertexAt gives them, its texels scrolled.
	 * With Held, a corner whose join holds is the model's it is joined to, as in the mesh.
	 */
	void AddChangedPolygon(const Weltall::FFieldScene& Scene, uint32 Index, TFunctionRef<FVector3f(uint16)> VertexAt, uint8 ScrollU, uint8 ScrollV,
		const FJoinsHeld* Held, const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw)
	{
		constexpr int32 Triangles[2][3] = {{0, 1, 2}, {1, 3, 2}};
		const Weltall::FFieldScenePolygon& Polygon = Scene.Polygons[Index];
		const Weltall::FMdlPrimitive& Primitive = Scene.PrimitiveOf(Polygon);
		const Weltall::FMdlPrimitiveTraits& Traits = Primitive.Traits();
		const uint32 Dynamic = Mesh.Polygons() + static_cast<uint32>(Draw.DynamicPolygons.Num());
		Draw.Redirects[Index] = static_cast<uint32>(Draw.DynamicCorners.Num());
		uint32 LowU = 255, LowV = 255, HighU = 0, HighV = 0;
		for (int32 Triangle = 0; Triangle < (Traits.Corners == 4 ? 2 : 1); ++Triangle)
		{
			for (const int32 Corner : Triangles[Triangle])
			{
				const Weltall::FMdlUv& Uv = Primitive.Uvs[static_cast<std::size_t>(Corner)];
				// The scroll wraps each corner within the page before the GPU interpolates between them.
				const uint32 U = static_cast<uint8>(Uv.U + ScrollU);
				const uint32 V = static_cast<uint8>(Uv.V + ScrollV);
				const uint16 Number = Primitive.Vertices[static_cast<std::size_t>(Corner)];
				FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
				Out.Position = VertexAt(Number);
				Out.TexCoord = FVector2f(static_cast<float>(U), static_cast<float>(V));
				Out.Polygon = Dynamic;
				Out.Object = Polygon.Object;
				Out.Corner = Traits.bGouraud ? static_cast<uint32>(Corner) : 0;
				if (const Weltall::FFieldSceneJoin* Join = Held != nullptr ? Scene.JoinOf(Polygon.Object, Number) : nullptr; Join != nullptr && Held->Holds(*Join))
				{
					Out.Position = FVector3f(Join->Point[0], Join->Point[1], Join->Point[2]);
					Out.Object = Join->Anchor;
				}
				LowU = FMath::Min(LowU, U);
				LowV = FMath::Min(LowV, V);
				HighU = FMath::Max(HighU, U);
				HighV = FMath::Max(HighV, V);
			}
		}
		FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
		State.Texture = FVramTexture::NoTexture;
		if (Traits.bTextured)
		{
			State.Texture = FVramTexture::Pack(Polygon.Page, Polygon.Clut);
			State.Flags = Primitive.IsRawTexture() ? FieldPolygonFlags::Raw : 0;
			State.TexelMin = LowU | LowV << 8;
			State.TexelMax = HighU | HighV << 8;
		}
		// A copy of the mesh's polygon: its flags as the mesh has them.
		State.Flags |= Mesh.States()[static_cast<int32>(Index)].Flags & (FieldPolygonFlags::OneSided | FieldPolygonFlags::SeeThrough);
		const FUintVector4 Colors = Draw.Colors[static_cast<int32>(Index)];
		Draw.Colors.Add(Colors);
	}
}

void AddFieldCharacterQuads(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw)
{
	const uint32 Identity = static_cast<uint32>(Draw.Objects.Num() / 3 - 1);
	TArray<FVector3f, TInlineAllocator<128>> Offsets;
	for (const Weltall::FFieldCharacterView& Character : Latest.Characters)
	{
		Offsets.Add(FieldCharacterOffset(Character, Previous, Alpha));
	}
	for (int32 Index = 0; Index < static_cast<int32>(Latest.Quads.size()); ++Index)
	{
		const Weltall::FFieldCharacterQuad& Entry = Latest.Quads[static_cast<std::size_t>(Index)];
		const Weltall::FSpriteQuad& Quad = Entry.Quad;
		const FVector3f& Offset = Offsets[Entry.Character];
		const uint32 Polygon = Mesh.Polygons() + static_cast<uint32>(Draw.DynamicPolygons.Num());
		for (const int32 Corner : QuadTriangleCorners)
		{
			FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
			Out.Position = ToVector(Quad.ViewCorner(static_cast<std::size_t>(Corner))) + Offset;
			Out.TexCoord = FVector2f((Corner & 1) != 0 ? Quad.U1 : Quad.U0, (Corner & 2) != 0 ? Quad.V1 : Quad.V0);
			Out.Polygon = Polygon;
			Out.Object = Identity;
			Out.Corner = 0;
		}
		FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
		// A sprite's parts may lie in VRAM's port rows (a pilot's stand-in).
		State.Texture = FVramTexture::Pack(Weltall::FTexturePage::FromPortAttribute(Quad.TexturePage), Weltall::FClutPosition::FromPortAttribute(Quad.Clut));
		State.Flags = FieldPolygonFlags::Sprite | ((Quad.ColorCode >> 24 & 1) != 0 ? FieldPolygonFlags::Raw : 0);
		if (!Latest.Characters[Entry.Character].bOnFloor)
		{
			State.Flags |= FieldPolygonFlags::AtEntry(Quad.OtIndex);
		}
		// The quads map their texels edge to edge: the last texel is one before the far edge.
		State.TexelMin = PackClampedTexel(FMath::Min(Quad.U0, Quad.U1), FMath::Min(Quad.V0, Quad.V1));
		State.TexelMax = PackClampedTexel(FMath::Max(Quad.U0, Quad.U1) - 1, FMath::Max(Quad.V0, Quad.V1) - 1);
		Draw.SpriteCells.Add(SpriteCellOf(Quad, State.Texture));
		const uint32 Color = Quad.ColorCode & 0xFFFFFF;
		Draw.Colors.Emplace(Color, Color, Color, Color);
	}
}

void AddFieldSeeThroughHoles(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha,
	const FFieldSceneMesh& Mesh, const FFieldRenderOptions& Options, FFieldSceneDraw& Draw, Weltall::FFieldSeeThroughFade* Fade)
{
	Draw.SeeThroughHoles.Reset();
	// The identity follows the scenery's models; the gears' rows follow it.
	Draw.SeeThroughObject = Mesh.FirstGearObject() - 1;
	Draw.SeeThroughWiden = Weltall::SeeThroughWiden(Options.SeeThrough);
	if (Options.SeeThrough == Weltall::EFieldSeeThrough::Off)
	{
		return;
	}
	TArray<std::array<float, 3>, TInlineAllocator<64>> Corners;
	for (int32 Number = 0; Number < static_cast<int32>(Latest.Characters.size()); ++Number)
	{
		const Weltall::FFieldCharacterView& Character = Latest.Characters[static_cast<std::size_t>(Number)];
		if (!Character.bSeenThrough || Character.bOnFloor || Draw.SeeThroughHoles.Num() == static_cast<int32>(Weltall::FieldSeeThroughHoles))
		{
			continue;
		}
		const FVector3f Offset = FieldCharacterOffset(Character, Previous, Alpha);
		Corners.Reset();
		for (const Weltall::FFieldCharacterQuad& Entry : Latest.Quads)
		{
			if (Entry.Character != Number)
			{
				continue;
			}
			for (std::size_t Corner = 0; Corner < 4; ++Corner)
			{
				const FVector3f At = ToVector(Entry.Quad.ViewCorner(Corner)) + Offset;
				Corners.Add({At.X, At.Y, At.Z});
			}
		}
		if (!Corners.IsEmpty())
		{
			Draw.SeeThroughHoles.Add(Weltall::SeeThroughHoleOf(std::span<const std::array<float, 3>>(Corners.GetData(), static_cast<std::size_t>(Corners.Num()))));
		}
	}
	// A gear's model that stands in for a member: its bones' parts' vertices, where their rows draw them.
	const std::vector<Weltall::FFieldGearLayoutItem>& Items = Latest.Gears.Items;
	const int32 FirstGear = static_cast<int32>(Mesh.FirstGearObject());
	if (!Draw.bGearsDrawn || Items.size() != Latest.Gears.Objects.size())
	{
		return;
	}
	for (const int32 Slot : Latest.SeenThroughGearSlots)
	{
		if (Draw.SeeThroughHoles.Num() == static_cast<int32>(Weltall::FieldSeeThroughHoles))
		{
			break;
		}
		Corners.Reset();
		for (int32 Object = 0; Object < static_cast<int32>(Items.size()); ++Object)
		{
			const Weltall::FFieldGearLayoutItem& Item = Items[static_cast<std::size_t>(Object)];
			const int32 Row = (FirstGear + Object) * 3;
			if (Item.Slot != Slot || Item.Part == nullptr || Item.Packets == nullptr || !Latest.Gears.Objects[static_cast<std::size_t>(Object)].bDrawn
				|| !Draw.Objects.IsValidIndex(Row + 2))
			{
				continue;
			}
			const FVector4f& X = Draw.Objects[Row];
			const FVector4f& Y = Draw.Objects[Row + 1];
			const FVector4f& Z = Draw.Objects[Row + 2];
			for (const Weltall::FGteVector& Vertex : Item.Part->Vertices)
			{
				const FVector4f Model(Vertex[0], Vertex[1], Vertex[2], 1.0f);
				const auto Dot = [&Model](const FVector4f& Rows) { return Rows.X * Model.X + Rows.Y * Model.Y + Rows.Z * Model.Z + Rows.W; };
				Corners.Add({Dot(X), Dot(Y), Dot(Z)});
			}
		}
		if (!Corners.IsEmpty())
		{
			Draw.SeeThroughHoles.Add(Weltall::SeeThroughHoleOf(std::span<const std::array<float, 3>>(Corners.GetData(), static_cast<std::size_t>(Corners.Num()))));
		}
	}
	MarkFieldSeeThroughModels(Scene, Latest, Mesh, Draw, Fade);
}

void MarkFieldSeeThroughModels(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw,
	Weltall::FFieldSeeThroughFade* Fade)
{
	const int32 Objects = static_cast<int32>(Scene.Objects.size());
	if (Draw.SeeThroughHoles.IsEmpty())
	{
		// Nothing hides a party that is not there: what was seen through comes back.
		if (Fade != nullptr)
		{
			TArray<bool> None;
			None.SetNumZeroed(Objects);
			Fade->Step(std::span<const bool>(None.GetData(), static_cast<std::size_t>(None.Num())), FPlatformTime::Seconds(), Latest.Tick);
		}
		return;
	}
	// The points of the party the eye looks for.
	TArray<std::array<float, 3>, TInlineAllocator<Weltall::FieldSeeThroughHoles * Weltall::FieldSeeThroughSamples>> Samples;
	for (const Weltall::FFieldSeeThroughHole& Hole : Draw.SeeThroughHoles)
	{
		for (const std::array<float, 3>& Sample : Weltall::SeeThroughSamples(Hole))
		{
			Samples.Add(Sample);
		}
	}
	const std::array<float, 3> Up{Draw.FloorUp.X, Draw.FloorUp.Y, Draw.FloorUp.Z};
	const bool bUp = !Draw.FloorUp.IsZero();
	const TConstArrayView<FFieldSceneCorner> MeshCorners = Mesh.CornerList();
	const auto Place = [&Draw](uint32 Object, const FVector3f& Point)
	{
		const int32 Row = static_cast<int32>(Object) * 3;
		if (!Draw.Objects.IsValidIndex(Row + 2))
		{
			return std::array<float, 3>{0.0f, 0.0f, 0.0f};
		}
		const auto Dot = [&Point](const FVector4f& Rows) { return Rows.X * Point.X + Rows.Y * Point.Y + Rows.Z * Point.Z + Rows.W; };
		return std::array<float, 3>{Dot(Draw.Objects[Row]), Dot(Draw.Objects[Row + 1]), Dot(Draw.Objects[Row + 2])};
	};
	// Per placed model: drawn with polygons that may be seen through, standing between the eye and the party, bearing one of them.
	TBitArray<> Seeable(false, Objects);
	TBitArray<> Hit(false, Objects);
	TBitArray<> Bearing(false, Objects);
	const uint32 SceneryPolygons = FMath::Min<uint32>(static_cast<uint32>(Scene.Polygons.size()), Mesh.FirstGearPolygon());
	for (uint32 Polygon = 0; Polygon < SceneryPolygons && Polygon < Latest.Scenery.Polygons.size(); ++Polygon)
	{
		const int32 Object = static_cast<int32>(Scene.Polygons[Polygon].Object);
		if (Object >= Objects || !Latest.Scenery.Polygons[Polygon].bDrawn || (Mesh.States()[static_cast<int32>(Polygon)].Flags & FieldPolygonFlags::SeeThrough) == 0)
		{
			continue;
		}
		Seeable[Object] = true;
		if (Hit[Object] && (Bearing[Object] || !bUp))
		{
			continue;
		}
		const bool bRedirected = Draw.Redirects.IsValidIndex(static_cast<int32>(Polygon)) && Draw.Redirects[Polygon] != FFieldSceneDraw::NoRedirect;
		const uint32 First = bRedirected ? Draw.Redirects[Polygon] : Mesh.FirstCorner(Polygon);
		const uint32 Count = Mesh.CornerCount(Polygon);
		const auto ViewAt = [&](uint32 Number)
		{
			const FFieldSceneCorner& Corner = bRedirected ? Draw.DynamicCorners[static_cast<int32>(First + Number)] : MeshCorners[static_cast<int32>(First + Number)];
			return Place(Corner.Object, Corner.Position);
		};
		for (uint32 Triangle = 0; Triangle + 2 < Count; Triangle += 3)
		{
			const std::array<float, 3> A = ViewAt(Triangle);
			const std::array<float, 3> B = ViewAt(Triangle + 1);
			const std::array<float, 3> C = ViewAt(Triangle + 2);
			for (int32 Sample = 0; Sample < Samples.Num() && !Hit[Object]; ++Sample)
			{
				Hit[Object] = Weltall::HidesFromEye(Samples[Sample], A, B, C);
			}
			for (int32 Hole = 0; bUp && Hole < Draw.SeeThroughHoles.Num() && !Bearing[Object]; ++Hole)
			{
				Bearing[Object] = Weltall::BearsSprite(Draw.SeeThroughHoles[Hole], Up, A, B, C);
			}
		}
	}
	// What a member stands on is never seen through; what hides one is, and so are the models it touches that stand above
	// that member's feet and in front of it, so a bridge or a wall made of several is seen through as one.
	TBitArray<> Hiding(false, Objects);
	TArray<FVector4f> Spheres;
	Spheres.SetNumZeroed(Objects);
	for (int32 Object = 0; Object < Objects; ++Object)
	{
		Hiding[Object] = Hit[Object] && !Bearing[Object];
		const Weltall::FFieldModelBounds& Bounds = Scene.Objects[static_cast<std::size_t>(Object)].Bounds;
		const std::array<float, 3> Centre = Place(static_cast<uint32>(Object), FVector3f(Bounds.Centre[0], Bounds.Centre[1], Bounds.Centre[2]));
		const int32 Row = Object * 3;
		const float Scale = Draw.Objects.IsValidIndex(Row) ? FVector3f(Draw.Objects[Row].X, Draw.Objects[Row].Y, Draw.Objects[Row].Z).Size() : 1.0f;
		Spheres[Object] = FVector4f(Centre[0], Centre[1], Centre[2], static_cast<float>(Bounds.Radius) * Scale);
	}
	const TBitArray<> Struck = Hiding;
	for (int32 Object = 0; Object < Objects; ++Object)
	{
		if (Hiding[Object] || !Seeable[Object] || Bearing[Object])
		{
			continue;
		}
		const FVector4f& Sphere = Spheres[Object];
		bool bInFront = false;
		for (const Weltall::FFieldSeeThroughHole& Hole : Draw.SeeThroughHoles)
		{
			const float Above = bUp ? (Sphere.X - Hole.Feet[0]) * Up[0] + (Sphere.Y - Hole.Feet[1]) * Up[1] + (Sphere.Z - Hole.Feet[2]) * Up[2] : Hole.Feet[1] - Sphere.Y;
			bInFront = bInFront || (Above > 0.0f && Sphere.Z < FMath::Min(Hole.Feet[2], Hole.Head[2]));
		}
		if (!bInFront)
		{
			continue;
		}
		for (int32 Other = 0; Other < Objects; ++Other)
		{
			const FVector4f& Touched = Spheres[Other];
			if (Struck[Other] && FVector3f(Sphere.X - Touched.X, Sphere.Y - Touched.Y, Sphere.Z - Touched.Z).Size() <= Sphere.W + Touched.W)
			{
				Hiding[Object] = true;
				break;
			}
		}
	}
	// How far each is seen through: after a moment and over a while where the drawing keeps a fade, at once where not.
	TArray<bool> Hides;
	Hides.SetNumUninitialized(Objects);
	for (int32 Object = 0; Object < Objects; ++Object)
	{
		Hides[Object] = Hiding[Object];
	}
	TArray<float> Immediate;
	std::span<const float> Shares;
	if (Fade != nullptr)
	{
		Shares = Fade->Step(std::span<const bool>(Hides.GetData(), static_cast<std::size_t>(Hides.Num())), FPlatformTime::Seconds(), Latest.Tick);
	}
	else
	{
		for (const bool bHides : Hides)
		{
			Immediate.Add(bHides ? 1.0f : 0.0f);
		}
		Shares = std::span<const float>(Immediate.GetData(), static_cast<std::size_t>(Immediate.Num()));
	}
	for (uint32 Polygon = 0; Polygon < SceneryPolygons; ++Polygon)
	{
		const int32 Object = static_cast<int32>(Scene.Polygons[Polygon].Object);
		if (Object >= Objects || static_cast<std::size_t>(Object) >= Shares.size() || !Draw.Colors.IsValidIndex(static_cast<int32>(Polygon)))
		{
			continue;
		}
		const uint32 Level = static_cast<uint32>(FMath::RoundToInt(FMath::Clamp(Shares[static_cast<std::size_t>(Object)], 0.0f, 1.0f) * FieldColorFlags::SeeThroughLevels));
		if (Level == 0)
		{
			continue;
		}
		Draw.Colors[static_cast<int32>(Polygon)].X |= Level << FieldColorFlags::SeeThroughShift;
		// A changed copy of it is drawn by colours of its own.
		if (Draw.Redirects.IsValidIndex(static_cast<int32>(Polygon)) && Draw.Redirects[Polygon] != FFieldSceneDraw::NoRedirect)
		{
			const uint32 Copy = Draw.DynamicCorners[static_cast<int32>(Draw.Redirects[Polygon])].Polygon;
			if (Draw.Colors.IsValidIndex(static_cast<int32>(Copy)))
			{
				Draw.Colors[static_cast<int32>(Copy)].X |= Level << FieldColorFlags::SeeThroughShift;
			}
		}
	}
}

void AddFieldBackdrop(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw)
{
	Draw.BackdropCorners = static_cast<uint32>(Draw.DynamicCorners.Num());
	Draw.BackdropCornerCount = 0;
	Draw.BackdropRegions.Reset();
	if (!Latest.Backdrop.bShown)
	{
		Draw.BackdropQuads.clear();
		return;
	}
	const std::vector<Weltall::FTextureRegion> Strip = Weltall::PanoramaTextureRegions(Latest.Backdrop.Panorama);
	Draw.BackdropRegions.Append(Strip.data(), static_cast<int32>(Strip.size()));
	const Weltall::FFieldBackdrop Backdrop = Previous != nullptr ? Weltall::BlendFieldBackdrop(Previous->Backdrop, Latest.Backdrop, Alpha) : Latest.Backdrop;
	Weltall::DrawFieldBackdrop(Backdrop, -BackdropBeyond, Weltall::FieldBackdropWidth + BackdropBeyond, Draw.BackdropQuads);
	const uint32 Identity = static_cast<uint32>(Draw.Objects.Num() / 3 - 1);
	// Far behind anything the field draws, placed where it projects onto the screen's pixels.
	const float Scale = BackdropDepth / FMath::Max(Draw.ProjectionDistance, 1.0f);
	for (const Weltall::FFieldBackdropQuad& Quad : Draw.BackdropQuads)
	{
		const bool bTextured = Quad.Kind == Weltall::FFieldBackdropQuad::EKind::Textured;
		const uint32 Polygon = Mesh.Polygons() + static_cast<uint32>(Draw.DynamicPolygons.Num());
		for (const int32 Corner : QuadTriangleCorners)
		{
			const std::array<int16, 2>& At = Quad.Corners[static_cast<std::size_t>(Corner)];
			FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
			Out.Position = FVector3f(static_cast<float>(At[0] - Weltall::FieldScreenCenterX) * Scale, static_cast<float>(At[1] - Weltall::FieldScreenCenterY) * Scale,
				BackdropDepth);
			const std::array<uint8, 2>& Texel = Quad.Texels[static_cast<std::size_t>(Corner)];
			Out.TexCoord = FVector2f(Texel[0], Texel[1]);
			Out.Polygon = Polygon;
			Out.Object = Identity;
			Out.Corner = Quad.Kind == Weltall::FFieldBackdropQuad::EKind::Graded ? static_cast<uint32>(Corner) : 0;
		}
		Draw.BackdropCornerCount += QuadCorners;
		FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
		State.Texture = FVramTexture::NoTexture;
		if (bTextured)
		{
			State.Texture = FVramTexture::Pack(Weltall::FTexturePage::FromAttribute(Quad.TexturePage), Weltall::FClutPosition::FromAttribute(Quad.Clut));
			State.Flags = FieldPolygonFlags::Raw;
			// The packet's texels run to the far column and row themselves.
			State.TexelMin = PackClampedTexel(FMath::Min(Quad.Texels[0][0], Quad.Texels[1][0]), FMath::Min(Quad.Texels[0][1], Quad.Texels[2][1]));
			State.TexelMax = PackClampedTexel(FMath::Max(Quad.Texels[0][0], Quad.Texels[1][0]), FMath::Max(Quad.Texels[0][1], Quad.Texels[2][1]));
		}
		const auto Color = [&Quad](std::size_t Corner) { return PackRgb(Quad.Colors[Corner][0], Quad.Colors[Corner][1], Quad.Colors[Corner][2]); };
		Draw.Colors.Emplace(Color(0), Color(1), Color(2), Color(3));
	}
}

void AddFieldParticleQuads(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw)
{
	const uint32 Identity = static_cast<uint32>(Draw.Objects.Num() / 3 - 1);
	Draw.ParticleCorners = static_cast<uint32>(Draw.DynamicCorners.Num());
	for (const Weltall::FFieldParticleQuad& Particle : Latest.Particles)
	{
		const Weltall::FFieldParticleQuad* Before = ParticleBefore(Particle, Previous);
		const uint32 Polygon = Mesh.Polygons() + static_cast<uint32>(Draw.DynamicPolygons.Num());
		int32 LowU = 255, LowV = 255, HighU = 0, HighV = 0;
		for (const int32 Corner : QuadTriangleCorners)
		{
			const std::size_t At = static_cast<std::size_t>(Corner);
			const FVector3f Now = ToVector(Particle.ViewCorner(At));
			FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
			Out.Position = Before != nullptr ? FMath::Lerp(ToVector(Before->ViewCorner(At)), Now, Alpha) : Now;
			Out.TexCoord = FVector2f(Particle.Texels[At][0], Particle.Texels[At][1]);
			Out.Polygon = Polygon;
			Out.Object = Identity;
			Out.Corner = 0;
		}
		for (const std::array<int16, 2>& Texel : Particle.Texels)
		{
			LowU = FMath::Min<int32>(LowU, Texel[0]);
			LowV = FMath::Min<int32>(LowV, Texel[1]);
			HighU = FMath::Max<int32>(HighU, Texel[0]);
			HighV = FMath::Max<int32>(HighV, Texel[1]);
		}
		FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
		State.Texture = FVramTexture::Pack(Weltall::FTexturePage::FromAttribute(Particle.TexturePage), Weltall::FClutPosition::FromAttribute(Particle.Clut));
		State.Flags = FieldPolygonFlags::Sprite | FieldPolygonFlags::AtEntry(Particle.OtIndex);
		State.TexelMin = PackClampedTexel(LowU, LowV);
		State.TexelMax = PackClampedTexel(HighU - 1, HighV - 1);
		std::array<uint8, 3> Color = Particle.Color;
		if (Before != nullptr)
		{
			for (std::size_t Channel = 0; Channel < 3; ++Channel)
			{
				Color[Channel] = static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(Before->Color[Channel]), static_cast<float>(Color[Channel]), Alpha)));
			}
		}
		const uint32 Packed = PackRgb(Color[0], Color[1], Color[2]);
		Draw.Colors.Emplace(Packed, Packed, Packed, Packed);
	}
}

void AddFieldChangedModels(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha,
	const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw)
{
	if (Latest.MovedModels.empty() && Latest.ScrolledModels.empty())
	{
		return;
	}
	if (Draw.Redirects.Num() != static_cast<int32>(Mesh.Polygons()))
	{
		Draw.Redirects.Init(FFieldSceneDraw::NoRedirect, static_cast<int32>(Mesh.Polygons()));
	}
	TArray<FVector3f, TInlineAllocator<1024>> Vertices;
	for (uint32 Object = 0; Object < Scene.Objects.size(); ++Object)
	{
		const auto Moved = std::find_if(Latest.MovedModels.begin(), Latest.MovedModels.end(), [Object](const Weltall::FFieldMovedModel& Candidate) { return Candidate.Object == Object; });
		const auto Scrolled = std::find_if(Latest.ScrolledModels.begin(), Latest.ScrolledModels.end(), [Object](const Weltall::FFieldScrolledModel& Candidate) { return Candidate.Object == Object; });
		if (Moved == Latest.MovedModels.end() && Scrolled == Latest.ScrolledModels.end())
		{
			continue;
		}
		const Weltall::FFieldSceneObject& Placed = Scene.Objects[Object];
		const std::vector<Weltall::FGteVector>& Rest = Scene.PartOf(Placed).Vertices;
		Vertices.Reset();
		for (const Weltall::FGteVector& Vertex : Rest)
		{
			Vertices.Emplace(Vertex[0], Vertex[1], Vertex[2]);
		}
		if (Moved != Latest.MovedModels.end() && Moved->Count == Rest.size())
		{
			const Weltall::FFieldMovedModel* Before = nullptr;
			if (Previous != nullptr)
			{
				const auto Found = std::find_if(Previous->MovedModels.begin(), Previous->MovedModels.end(), [Object](const Weltall::FFieldMovedModel& Candidate) { return Candidate.Object == Object; });
				Before = Found != Previous->MovedModels.end() && Found->Count == Moved->Count ? &*Found : nullptr;
			}
			for (uint32 Vertex = 0; Vertex < Moved->Count; ++Vertex)
			{
				const Weltall::FGteVector& Now = Latest.Vertices[Moved->First + Vertex];
				FVector3f Drawn(Now[0], Now[1], Now[2]);
				if (Before != nullptr)
				{
					const Weltall::FGteVector& Then = Previous->Vertices[Before->First + Vertex];
					Drawn = FMath::Lerp(FVector3f(Then[0], Then[1], Then[2]), Drawn, Alpha);
				}
				Vertices[static_cast<int32>(Vertex)] = Drawn;
			}
		}
		const uint8 ScrollU = Scrolled != Latest.ScrolledModels.end() ? Scrolled->U : 0;
		const uint8 ScrollV = Scrolled != Latest.ScrolledModels.end() ? Scrolled->V : 0;
		// Moved vertices are the model's own wherever they are; scrolled texels leave its joins as they hold.
		const FJoinsHeld Held = JoinsHeld(Latest, Previous);
		const bool bMoved = Moved != Latest.MovedModels.end();
		for (uint32 Index = Placed.FirstPolygon; Index < Placed.FirstPolygon + Placed.PolygonCount; ++Index)
		{
			AddChangedPolygon(Scene, Index, [&Vertices](uint16 Number) { return Vertices[Number]; }, ScrollU, ScrollV, bMoved ? nullptr : &Held, Mesh, Draw);
		}
	}
}

void AddFieldReleasedJoins(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw)
{
	const TConstArrayView<uint32> Joined = Mesh.JoinedPolygons();
	if (Joined.IsEmpty() || Latest.Scenery.Objects.size() != Scene.Objects.size())
	{
		return;
	}
	const FJoinsHeld Held = JoinsHeld(Latest, Previous);
	bool bAllStand = true;
	for (uint32 Object = 0; Object < Scene.Objects.size() && bAllStand; ++Object)
	{
		bAllStand = Held.Stands(Object);
	}
	if (bAllStand)
	{
		return;
	}
	if (Draw.Redirects.Num() != static_cast<int32>(Mesh.Polygons()))
	{
		Draw.Redirects.Init(FFieldSceneDraw::NoRedirect, static_cast<int32>(Mesh.Polygons()));
	}
	for (const uint32 Index : Joined)
	{
		if (Draw.Redirects[Index] != FFieldSceneDraw::NoRedirect)
		{
			continue;
		}
		const Weltall::FFieldScenePolygon& Polygon = Scene.Polygons[Index];
		const Weltall::FMdlPrimitive& Primitive = Scene.PrimitiveOf(Polygon);
		bool bHolds = true;
		for (int32 Corner = 0; Corner < Primitive.Traits().Corners && bHolds; ++Corner)
		{
			const Weltall::FFieldSceneJoin* Join = Scene.JoinOf(Polygon.Object, Primitive.Vertices[static_cast<std::size_t>(Corner)]);
			bHolds = Join == nullptr || Held.Holds(*Join);
		}
		if (bHolds)
		{
			continue;
		}
		const std::vector<Weltall::FGteVector>& Own = Scene.PartOf(Scene.Objects[Polygon.Object]).Vertices;
		AddChangedPolygon(Scene, Index, [&Own](uint16 Number) { return FVector3f(Own[Number][0], Own[Number][1], Own[Number][2]); }, 0, 0, &Held, Mesh, Draw);
	}
}
