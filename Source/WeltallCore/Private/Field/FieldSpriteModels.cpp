#include "Field/FieldSpriteModels.h"

#include "Field/FieldRenderState.h"
#include "Graphics/GteVectors.h"
#include "Graphics/SpriteFile.h"

#include <algorithm>
#include <string>

namespace Weltall
{
	namespace
	{
		/** A page command's bits a texture override keeps: the depth and the blend mode, its place the override's. */
		constexpr uint16 PageModeBits = 0xFFE0;
		/** The polygons' keys: the child's number, then the polygon's in its part. */
		constexpr uint32 SpriteModelPolygonBits = 12;

		/** The page place GetTPage gives a VRAM position: its 64-unit column and its 256-line row. */
		uint16 SpriteModelPagePlace(int16 X, int16 Y)
		{
			return static_cast<uint16>((Y & 0x100) >> 4 | (X & 0x3FF) >> 6);
		}

		/** The kinds the fog's draw mode depth-cues: flat-coloured textured triangles and quads. */
		bool IsFogCuedKind(EMdlPrimitiveType Type)
		{
			return Type == EMdlPrimitiveType::TexturedTriangle || Type == EMdlPrimitiveType::StaticTexturedTriangle || Type == EMdlPrimitiveType::TexturedQuad
				|| Type == EMdlPrimitiveType::StaticTexturedQuad;
		}

		TResult<std::unique_ptr<FFieldSpriteModelPart>> BuildSpriteModelPart(const FFieldSpriteModelDraw& Draw)
		{
			auto Built = std::make_unique<FFieldSpriteModelPart>();
			Built->File = Draw.File;
			Built->Offset = Draw.Part;
			Built->bTextureOverride = Draw.bTextureOverride;
			Built->TextureX = Draw.TextureX;
			Built->TextureY = Draw.TextureY;
			// The part's offsets count from the part itself.
			TResult<FMdlPart> Part = ParseMdlPart(Draw.File->From(Draw.Part));
			if (!Part)
			{
				return MakeError("a sprite's model at " + std::to_string(Draw.Part) + ": " + Part.Error().Message);
			}
			Built->Part = std::move(*Part);
			TResult<std::vector<FMdlPacket>> Packets = BuildMdlPackets(Built->Part, 0, FGte{});
			if (!Packets)
			{
				return MakeError("a sprite's model at " + std::to_string(Draw.Part) + ": " + Packets.Error().Message);
			}
			Built->Packets = std::move(*Packets);
			const uint16 Place = SpriteModelPagePlace(Draw.TextureX, Draw.TextureY);
			for (const FMdlPrimitive& Primitive : Built->Part.Primitives)
			{
				if (Primitive.Traits().bTextured && (Primitive.bInheritsTexturePage || Primitive.bInheritsClut))
				{
					return MakeError("a sprite's model at " + std::to_string(Draw.Part) + " takes its first page or CLUT from the model built before it");
				}
				FMdlTextureState Texture{Primitive.TexturePage, Primitive.Clut};
				if (Draw.bTextureOverride && Primitive.Traits().bTextured)
				{
					Texture.TexturePage = static_cast<uint16>((Texture.TexturePage & PageModeBits) | Place);
				}
				Built->Textures.push_back(Texture);
			}
			return Built;
		}
	}

	TResult<const FFieldSpriteModelPart*> FFieldSpriteModelParts::Find(const FFieldSpriteModelDraw& Draw)
	{
		for (const std::unique_ptr<FFieldSpriteModelPart>& Part : Parts)
		{
			if (Part->File == Draw.File && Part->Offset == Draw.Part && Part->bTextureOverride == Draw.bTextureOverride
				&& (!Draw.bTextureOverride || (Part->TextureX == Draw.TextureX && Part->TextureY == Draw.TextureY)))
			{
				return Part.get();
			}
		}
		TResult<std::unique_ptr<FFieldSpriteModelPart>> Built = BuildSpriteModelPart(Draw);
		if (!Built)
		{
			return Built.Error();
		}
		Parts.push_back(std::move(*Built));
		return Parts.back().get();
	}

	void EvaluateFieldSpriteModels(std::span<const FFieldSpriteModelDrawn> Models, const FFieldView& View, const FFieldFog& Fog, int32 FarBucket, EFieldCulling Culling,
		FFieldSpriteModelsFrame& Frame)
	{
		Frame.Models.clear();
		Frame.Polygons.clear();
		FGte Drawing = FieldDrawingGte(View);
		if (Fog.bOn)
		{
			// As the field sets the GTE up before its models; the children are drawn after them.
			Drawing.FarColor = {Fog.FarColor[0] << 4, Fog.FarColor[1] << 4, Fog.FarColor[2] << 4};
			Drawing.SetFogNearFar(Fog.Near, Fog.Far, View.ProjectionDistance);
		}
		for (const FFieldSpriteModelDrawn& Draw : Models)
		{
			const FFieldSpriteModelPart& Built = *Draw.Part;
			const FMdlPart& Part = Built.Part;
			const uint32 Model = static_cast<uint32>(Frame.Models.size());
			FFieldSpriteModelView& Placed = Frame.Models.emplace_back();
			// The sprite's matrix under the kernel's view: the whole of the view, the sprite's place 16 bits a component.
			Placed.Matrix = CompMatrix(View.View, Draw.Matrix);
			Placed.World = Draw.Matrix.T;
			FGte Gte = Drawing;
			Gte.Transform = Placed.Matrix;
			Frame.Projected.resize(Part.Vertices.size());
			for (std::size_t Vertex = 0; Vertex < Part.Vertices.size(); ++Vertex)
			{
				Frame.Projected[Vertex] = Gte.Rtps(Part.Vertices[Vertex]);
			}
			for (std::size_t Number = 0; Number < Part.Primitives.size(); ++Number)
			{
				const FMdlPrimitive& Primitive = Part.Primitives[Number];
				const FMdlPrimitiveTraits& Traits = Primitive.Traits();
				FFieldSpriteModelPolygon& Polygon = Frame.Polygons.emplace_back();
				Polygon.Model = Model;
				Polygon.BlendKey = Draw.Key << SpriteModelPolygonBits | static_cast<uint32>(Number);
				Polygon.Type = Primitive.Type;
				Polygon.CornerCount = Traits.Corners;
				for (std::size_t Corner = 0; Corner < Traits.Corners; ++Corner)
				{
					const uint16 Vertex = Primitive.Vertices[Corner];
					Polygon.Corners[Corner] = Part.Vertices[Vertex];
					Polygon.ScreenX[Corner] = Frame.Projected[Vertex].ScreenX;
					Polygon.ScreenY[Corner] = Frame.Projected[Vertex].ScreenY;
				}
				Polygon.Uvs = Primitive.Uvs;
				Polygon.TexturePage = Built.Textures[Number].TexturePage;
				Polygon.Clut = Built.Textures[Number].Clut;
				Polygon.Code = Built.Packets[Number].Code;
				Polygon.Colors = Built.Packets[Number].Colors;
				// Every kind is sorted by its average depth, in fog too.
				const FFieldPolygonTest Test = TestFieldPolygon(Primitive, Frame.Projected.data(), EFieldDrawMode::Average, Gte);
				Polygon.Bucket = Test.Bucket;
				Polygon.Dropped = Test.Dropped;
				const bool bBeyond = Polygon.Bucket > FarBucket;
				if (bBeyond)
				{
					Polygon.Dropped |= FieldPolygonDrop::BeyondFarBucket;
				}
				Polygon.bDrawn = Culling == EFieldCulling::Original ? Polygon.Dropped == 0 : !bBeyond && Test.bInRangeAnyFacing;
				Polygon.bFogged = Draw.bFogged && IsFogCuedKind(Primitive.Type);
				if (Polygon.bFogged)
				{
					// The fog's base colour depth-cued by the last corner projected, which then modulates the texels.
					const FGteProjection& Last = Frame.Projected[Primitive.Vertices[static_cast<std::size_t>(Traits.Corners - 1)]];
					const FGteColor Faded = Gte.DepthCueColor({Fog.Color[0], Fog.Color[1], Fog.Color[2], 0}, Last.DepthCue);
					Polygon.Colors.fill({Faded.R, Faded.G, Faded.B});
					Polygon.Code = static_cast<uint8>(Polygon.Code & ~1u);
				}
			}
		}
	}
}
