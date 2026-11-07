#include "Field/FieldPresenter.h"

#include "Field/FieldLight.h"
#include "Field/FieldRuntime.h"
#include "Field/FieldSprites.h"
#include "Field/FieldWalkmesh.h"
#include "Graphics/Gte.h"
#include "Graphics/GteVectors.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		/** The GTE's scale for the average depth of four corners, as the field sets it. */
		constexpr int16 AverageScale4 = 0x100;
		/** A shadow: a 48-unit square in its floor's plane, the 16 x 16 blob of the system images drawn subtractive. */
		constexpr int16 ShadowHalf = 24;
		constexpr uint16 ShadowPage = 0x5A;
		constexpr uint16 ShadowClut = 0x3CD0;
		constexpr uint32 ShadowColorCode = 0x2E808080;
		/** The blob's 16 x 16 texels, edge to edge across the square. */
		constexpr int16 ShadowFirstTexel = 0;
		constexpr int16 ShadowLastTexel = 16;
		constexpr int16 ShadowFirstRow = 224;
		constexpr int16 ShadowLastRow = 240;
		/** Floors a shadow is not drawn on. */
		constexpr uint32 ShadowlessFloor = FieldFloorAttribute::Ladder | 0x2;
		constexpr uint32 ShadowKey = 7;
		constexpr uint32 KeyShift = 3;
		/** The kernel's draw modes the field gives its models: relit, sorted by the farthest corner, and the two in fog. */
		constexpr uint8 RelitMode = 1;
		constexpr uint8 FarthestMode = 2;
		constexpr uint8 FogMode = 4;
		constexpr uint8 FogFarthestMode = 5;
	}

	void FFieldPresenter::CaptureScenery(const FEventContext& Context, EFieldCulling Culling, const FFieldPresentedView& Shown, FFieldFrame& Frame)
	{
		FFieldScene& Scene = *MapScene;
		const FFieldRenderState& Render = Context.Render;
		Scene.State.BackgroundColor = Render.BackgroundColor;
		// The far bucket moves back with the player's eye, so the far cut stays where the scripts put it.
		Scene.State.FarBucket = Shown.FarBucket(Render.FarBucket);
		Scene.State.CullMarginX = Render.CullMarginX;
		Scene.State.CullMarginY = Render.CullMarginY;
		Scene.State.SceneryDrift = Render.SceneryDrift;
		Scene.State.Fog = Render.Fog;
		const bool bDriftDrawsAll = (Render.SceneryDrift & FieldSceneryDrift::DrawnOffScreen) != 0;
		Frame.SceneryWorld.resize(Scene.Objects.size());
		Frame.MovedModels.clear();
		Frame.Vertices.clear();
		Frame.ScrolledModels.clear();
		for (std::size_t Number = 0; Number < Scene.Objects.size(); ++Number)
		{
			// The records as the tick's drawing of the models places them: the scripts' matrices and translations.
			FFieldSceneObject& Object = Scene.Objects[Number];
			const FFieldActor& Record = Context.Actors[Object.Actor];
			Object.bHidden = (Record.Flags & (FieldActorFlag::Hidden | FieldActorFlag::NoModel)) != 0;
			Object.bFogged = Record.DrawMode == FogMode || Record.DrawMode == FogFarthestMode;
			switch (Record.DrawMode)
			{
			case RelitMode:
				Object.DrawMode = EFieldDrawMode::Relit;
				break;
			case FarthestMode:
			case FogFarthestMode:
				Object.DrawMode = EFieldDrawMode::Farthest;
				break;
			default:
				Object.DrawMode = EFieldDrawMode::Average;
				break;
			}
			if (Record.DrawnVertices.size() == Scene.PartOf(Object).Vertices.size() && !Record.DrawnVertices.empty())
			{
				Frame.MovedModels.push_back({static_cast<uint32>(Number), static_cast<uint32>(Frame.Vertices.size()), static_cast<uint32>(Record.DrawnVertices.size())});
				Frame.Vertices.insert(Frame.Vertices.end(), Record.DrawnVertices.begin(), Record.DrawnVertices.end());
			}
			const FMdlUv Scroll = Render.ModelUvScroll(Object.Actor);
			if (Scroll.U != 0 || Scroll.V != 0)
			{
				Frame.ScrolledModels.push_back({static_cast<uint32>(Number), Scroll.U, Scroll.V});
			}
			PlaceFieldSceneObject(Context, Object, bDriftDrawsAll);
			const std::array<int32, 3>& At = Object.bWithoutWorldRotation ? Record.Position : Object.Placement.T;
			Frame.SceneryWorld[Number] = At;
		}
		for (const FFieldMovedModel& Moved : Frame.MovedModels)
		{
			Scene.Objects[Moved.Object].MovedVertices = std::span<const FGteVector>(Frame.Vertices).subspan(Moved.First, Moved.Count);
		}
		EvaluateFieldScene(Scene, Shown.View, Culling, Frame.Scenery);
		for (const FFieldMovedModel& Moved : Frame.MovedModels)
		{
			Scene.Objects[Moved.Object].MovedVertices = {};
		}
	}

	void FFieldPresenter::MoveCharacters(const FGteMatrix& From, const FGteMatrix& To, FFieldFrame& Frame)
	{
		// A pass is placed by its sprite's position through the view, which is all that changes: its
		// rotation is the screen's, the same in both. Its entry moves with its depth.
		CharacterMoves.resize(Frame.Characters.size());
		for (std::size_t Number = 0; Number < Frame.Characters.size(); ++Number)
		{
			FFieldCharacterView& Character = Frame.Characters[Number];
			const FGteVector Position = {static_cast<int16>(Character.World[0]), static_cast<int16>(Character.World[1]), static_cast<int16>(Character.World[2])};
			const FGteLongVector Before = ApplyMatrix(From, Position);
			const FGteLongVector After = ApplyMatrix(To, Position);
			std::array<int32, 4>& Move = CharacterMoves[Number];
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Move[Axis] = (After[Axis] + To.T[Axis]) - (Before[Axis] + From.T[Axis]);
				Character.Origin[Axis] += static_cast<float>(Move[Axis]);
			}
			Move[3] = ((After[2] + To.T[2]) >> 4) - ((Before[2] + From.T[2]) >> 4);
		}
		for (FFieldCharacterQuad& Quad : Frame.Quads)
		{
			const std::array<int32, 4>& Move = CharacterMoves[Quad.Character];
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Quad.Quad.Matrix.T[Axis] += Move[Axis];
			}
			Quad.Quad.OtIndex += Move[3];
		}
	}

	void FFieldPresenter::CaptureShadows(const FFieldRuntime& Runtime, const FGteMatrix& View, FFieldFrame& Frame)
	{
		const FEventContext& Context = Runtime.Context();
		FGte Gte;
		Gte.OffsetX = FieldScreenCenterX << 16;
		Gte.OffsetY = FieldScreenCenterY << 16;
		Gte.ProjectionDistance = static_cast<uint16>(Context.Camera.ProjectionDistance);
		Gte.AverageScale4 = AverageScale4;
		constexpr std::array<FGteVector, 4> Corners{{{ShadowHalf, 0, ShadowHalf}, {-ShadowHalf, 0, ShadowHalf}, {ShadowHalf, 0, -ShadowHalf},
			{-ShadowHalf, 0, -ShadowHalf}}};
		for (std::size_t Actor = 0; Actor < Context.States.size(); ++Actor)
		{
			const FFieldActor& Record = Context.Actors[Actor];
			const FEventActorState& State = Context.States[Actor];
			if ((Record.Flags & (FieldActorFlag::NoModel | FieldActorFlag::Hidden)) != FieldActorFlag::NoModel
				|| (State.Flags1 & (EventActorFlags1::Removed | EventActorFlags1::Gear | EventActorFlags1::OffScreen | EventActorFlags1::NotFloorAligned)) != 0
				|| (State.Flags0 & EventActorFlags0::MovedByScript) != 0 || (State.FloorAttributes & ShadowlessFloor) != 0)
			{
				continue;
			}
			const FSprite* Sprite = Runtime.Sprites().SpriteOf(static_cast<int32>(Actor));
			if (Sprite == nullptr)
			{
				continue;
			}
			// Rows across, up and along the floor from its normal, so the shadow lies on a slope.
			const FGteLongVector& Normal = State.FloorNormal;
			const FGteLongVector Across = VectorNormal(OuterProduct12({0, 0, FGteMatrix::One}, Normal));
			const FGteLongVector Along = VectorNormal(OuterProduct12(Across, Normal));
			FGteMatrix Floor;
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Floor.M[0][Axis] = static_cast<int16>(Across[Axis]);
				Floor.M[1][Axis] = static_cast<int16>(Normal[Axis]);
				Floor.M[2][Axis] = static_cast<int16>(Along[Axis]);
			}
			const FGteVector Base = {static_cast<int16>(Record.Translation[0]), Sprite->FloorY, static_cast<int16>(Record.Translation[2])};
			std::array<int32, 3> Scale{};
			const bool bGearMember = Context.PilotSpriteBase != 0 && (State.Flags0 & EventActorFlags0::PartyMember) != 0;
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Scale[Axis] = State.Scale[Axis] * 0xC00 >> (bGearMember ? 14 : 12);
			}
			FGteMatrix Matrix = ScaleColumns(MulMatrix(View, Floor), Scale);
			Matrix.T = RotTrans(View, Base);
			Gte.Transform = Matrix;
			std::array<uint16, 4> Depths{};
			FFieldCharacterQuad& Shadow = Frame.Quads.emplace_back();
			Shadow.Quad.Matrix = Matrix;
			Shadow.Quad.Corners = Corners;
			for (std::size_t Corner = 0; Corner < 4; ++Corner)
			{
				Depths[Corner] = Gte.Rtps(Corners[Corner]).ScreenZ;
			}
			Shadow.Quad.U0 = ShadowFirstTexel;
			Shadow.Quad.U1 = ShadowLastTexel;
			Shadow.Quad.V0 = ShadowFirstRow;
			Shadow.Quad.V1 = ShadowLastRow;
			Shadow.Quad.TexturePage = ShadowPage;
			Shadow.Quad.Clut = ShadowClut;
			Shadow.Quad.ColorCode = ShadowColorCode;
			Shadow.Quad.OtIndex = Gte.AverageDepth4(Depths[0], Depths[1], Depths[2], Depths[3]) >> 2;
			FFieldCharacterView& Character = Frame.Characters.emplace_back();
			Character.BlendKey = static_cast<uint32>(Actor) << KeyShift | ShadowKey;
			Character.Origin = {static_cast<float>(Matrix.T[0]), static_cast<float>(Matrix.T[1]), static_cast<float>(Matrix.T[2])};
			Character.World = {Base[0], Base[1], Base[2]};
			Character.bOnFloor = true;
			Shadow.Character = static_cast<uint16>(Frame.Characters.size() - 1);
		}
	}

	void FFieldPresenter::DrawSpriteModel(const FFieldSpriteModelDraw& Draw)
	{
		if (TickingRuntime == nullptr || SpriteModelError)
		{
			return;
		}
		// Another map's sprite files: the parts built from the last map's go.
		if (TickingRuntime->MapLoads() != SpriteModelLoads)
		{
			SpriteModelParts.Clear();
			SpriteModelsShown.clear();
			SpriteModelLoads = TickingRuntime->MapLoads();
		}
		TResult<const FFieldSpriteModelPart*> Part = SpriteModelParts.Find(Draw);
		if (!Part)
		{
			SpriteModelError = Part.Error();
			return;
		}
		SpriteModelCapture.push_back({*Part, Draw.Matrix, Draw.bFogged, Draw.Key});
	}

	void FFieldPresenter::OrderFrame(FFieldFrame& Frame, int32 FarBucketShift)
	{
		const FFieldScene& Scene = *MapScene;
		const std::vector<uint32>& Scenery = Frame.Scenery.DrawOrder;
		Frame.Order.clear();
		std::size_t Next = 0;
		const int32 Far = std::min<int32>(Scene.State.FarBucket, 0xFFF);
		// The background table first, then the main table far to near. The children's models were linked
		// into it after the scenery, the characters after them, the particles after those and the gears
		// last, so within an entry the gears come first, then the particles, the characters, the
		// children's models and the scenery, each the last linked first. The panorama lies in the background table's entry before the far bucket, linked after
		// its scenery. Before the table's first entry it would lie outside the tables drawn.
		Frame.Backdrop.bShown = Frame.Backdrop.bShown && Far >= 1;
		Frame.Backdrop.Entry = Far - 1;
		bool bBackdrop = Frame.Backdrop.bShown;
		for (; Next < Scenery.size() && Scene.Objects[Scene.Polygons[Scenery[Next]].Object].bBackground; ++Next)
		{
			if (bBackdrop && Frame.Scenery.Polygons[Scenery[Next]].Bucket < Far)
			{
				Frame.Order.push_back({0, EFieldDrawKind::Backdrop});
				bBackdrop = false;
			}
			Frame.Order.push_back({Scenery[Next], EFieldDrawKind::Scenery});
		}
		if (bBackdrop)
		{
			Frame.Order.push_back({0, EFieldDrawKind::Backdrop});
		}
		const auto SortLinked = [Far](std::vector<uint32>& Linked, std::size_t Count, auto&& EntryOf)
		{
			Linked.clear();
			for (std::size_t Item = 0; Item < Count; ++Item)
			{
				const int32 Entry = EntryOf(Item);
				if (Entry >= 0 && Entry <= Far)
				{
					Linked.push_back(static_cast<uint32>(Item));
				}
			}
			std::sort(Linked.begin(), Linked.end(), [&EntryOf](uint32 A, uint32 B)
			{
				const int32 EntryA = EntryOf(A);
				const int32 EntryB = EntryOf(B);
				return EntryA != EntryB ? EntryA > EntryB : A > B;
			});
		};
		// A sprite's, a particle's and a child model's entry is the game's, sorted by the game's camera: the player's own
		// framing draws the picture's depths elsewhere, and the far bucket these are kept under has moved with them
		// (FFieldPresentedView::FarBucket), so their entries move by as much. Nearer than the table's first entry they
		// wait there, which draws them in front of it.
		const auto Moved = [FarBucketShift](int32 Entry) { return Entry < 0 ? -1 : std::max(0, Entry + FarBucketShift); };
		const auto CharacterEntry = [&Frame, &Moved](std::size_t Quad) { return Moved(Frame.Quads[Quad].Quad.OtIndex); };
		const auto ParticleEntry = [&Frame, &Moved](std::size_t Particle) { return Moved(Frame.Particles[Particle].OtIndex); };
		SortLinked(Sorted, Frame.Quads.size(), CharacterEntry);
		SortLinked(SortedParticles, Frame.Particles.size(), ParticleEntry);
		// The children's models link only what they draw.
		const std::vector<FFieldSpriteModelPolygon>& ChildPolygons = Frame.SpriteModels.Polygons;
		const auto ChildEntry = [&ChildPolygons, &Moved](std::size_t Polygon)
		{
			return ChildPolygons[Polygon].bDrawn ? Moved(static_cast<int32>(ChildPolygons[Polygon].Bucket)) : -1;
		};
		SortLinked(SpriteModelsSorted, ChildPolygons.size(), ChildEntry);
		// The gears link only what they draw.
		const std::vector<FFieldGearLink>& Links = Frame.Gears.Links;
		GearsSorted.clear();
		for (std::size_t Link = 0; Link < Links.size(); ++Link)
		{
			GearsSorted.push_back(static_cast<uint32>(Link));
		}
		std::sort(GearsSorted.begin(), GearsSorted.end(), [&Links](uint32 A, uint32 B)
		{
			return Links[A].Bucket != Links[B].Bucket ? Links[A].Bucket > Links[B].Bucket : A > B;
		});
		std::size_t Character = 0;
		std::size_t Particle = 0;
		std::size_t Gear = 0;
		std::size_t Child = 0;
		while (Next < Scenery.size() || Character < Sorted.size() || Particle < SortedParticles.size() || Gear < GearsSorted.size()
			|| Child < SpriteModelsSorted.size())
		{
			const int32 SceneryEntry = Next < Scenery.size() ? Frame.Scenery.Polygons[Scenery[Next]].Bucket : -1;
			const int32 NextCharacter = Character < Sorted.size() ? CharacterEntry(Sorted[Character]) : -1;
			const int32 NextParticle = Particle < SortedParticles.size() ? ParticleEntry(SortedParticles[Particle]) : -1;
			const int32 NextGear = Gear < GearsSorted.size() ? Links[GearsSorted[Gear]].Bucket : -1;
			const int32 NextChild = Child < SpriteModelsSorted.size() ? ChildEntry(SpriteModelsSorted[Child]) : -1;
			if (NextGear >= 0 && NextGear >= NextParticle && NextGear >= NextCharacter && NextGear >= NextChild && NextGear >= SceneryEntry)
			{
				const FFieldGearLink& Link = Links[GearsSorted[Gear++]];
				Frame.Order.push_back({Link.Index, EFieldDrawKind::Gear, Link.Kind});
			}
			else if (NextParticle >= 0 && NextParticle >= NextCharacter && NextParticle >= NextChild && NextParticle >= SceneryEntry)
			{
				Frame.Order.push_back({SortedParticles[Particle++], EFieldDrawKind::Particle});
			}
			else if (NextCharacter >= 0 && NextCharacter >= NextChild && NextCharacter >= SceneryEntry)
			{
				Frame.Order.push_back({Sorted[Character++], EFieldDrawKind::Character});
			}
			else if (NextChild >= 0 && NextChild >= SceneryEntry)
			{
				Frame.Order.push_back({SpriteModelsSorted[Child++], EFieldDrawKind::SpriteModel});
			}
			else
			{
				Frame.Order.push_back({Scenery[Next++], EFieldDrawKind::Scenery});
			}
		}
	}
}
