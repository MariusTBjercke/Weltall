#pragma once

#include "Field/FieldBackdrop.h"
#include "Field/FieldCamera.h"
#include "Field/FieldCameraOffsets.h"
#include "Input/ButtonGlyphs.h"
#include "Field/FieldGearDrawing.h"
#include "Field/FieldSceneFrame.h"
#include "Field/FieldScreenInVram.h"
#include "Field/FieldSpriteModels.h"
#include "Field/FieldScrolls.h"
#include "Foundation/Types.h"
#include "Graphics/SpriteDrawing.h"
#include "Graphics/Vram.h"
#include "Presentation/DrawList.h"

#include <array>
#include <vector>

namespace Weltall
{
	/**
	 * Something the characters' drawing draws: an actor's sprite in one of its passes, or its shadow.
	 * Its quads move with it between ticks.
	 */
	struct FFieldCharacterView
	{
		/** The same for the same actor's same pass, or its shadow, from tick to tick: the display pairs them. */
		uint32 BlendKey = 0;
		/** Where it is in view space: the display moves its quads by how far this moves between ticks. */
		std::array<float, 3> Origin{};
		/** The actor's place in the world, whole units: one that jumps between ticks is not drawn in between. */
		std::array<int32, 3> World{};
		/** A shadow lying on its floor, rather than a sprite facing the screen. */
		bool bOnFloor = false;
		/** A party member's sprite outside cutscenes: the scenery in front of it may be seen through about it (EFieldSeeThrough). */
		bool bSeenThrough = false;
		/** Which of the sprite's pieces the pass drew: all of them, or those on one side of its wading line. */
		ESpriteClip Clip = ESpriteClip::None;
	};

	/** A quad of the characters' drawing: a sprite's part or piece, or a shadow. */
	struct FFieldCharacterQuad
	{
		FSpriteQuad Quad;
		/** What it moves with, in FFieldFrame::Characters. */
		uint16 Character = 0;
	};

	/**
	 * A particle as the field's effects draw it: a semi-transparent quad facing the screen, its corners
	 * put in view space (a view unit is a third of a world unit) by the matrix the GTE projects them
	 * with, textured from the effects' texture page.
	 */
	struct FFieldParticleQuad
	{
		/** The same for the same particle of the same effect's emitter from tick to tick. */
		uint32 BlendKey = 0;
		/** Its life left: a particle drawn in the tick before with one more is the same one, moved on. */
		int16 Life = 0;
		FGteMatrix Matrix;
		/** Top left, top right, bottom left, bottom right, before the matrix. */
		std::array<FGteVector, 4> Corners{};
		/** Each corner's texel (u, v) in the page, edge to edge (a far edge can lie at 256). */
		std::array<std::array<int16, 2>, 4> Texels{};
		/** The page (its blend mode the particle's) and the CLUT, as the GPU's attributes. */
		uint16 TexturePage = 0;
		uint16 Clut = 0;
		/** The colour its texels are modulated by. */
		std::array<uint8, 3> Color{};
		int32 OtIndex = 0;
		/** Where it is in the world (whole units): one that jumps between ticks is not drawn in between. */
		std::array<int32, 3> World{};

		/** A corner in view space: through the matrix, without the GTE's rounding. */
		std::array<float, 3> ViewCorner(std::size_t Corner) const { return ViewPoint(Matrix, Corners[Corner]); }
	};

	/** What an entry of the painter's order is. */
	enum class EFieldDrawKind : uint8
	{
		/** A polygon of the scenery (FFieldSceneFrame::Polygons). */
		Scenery,
		/** A quad of the characters (FFieldFrame::Quads). */
		Character,
		/** A particle (FFieldFrame::Particles). */
		Particle,
		/** The panorama (FFieldFrame::Backdrop), all its packets. */
		Backdrop,
		/** Something a gear draws (FFieldFrame::Gears): which, by FFieldDrawItem::Gear. */
		Gear,
		/** A polygon of a spawned child's model (FFieldFrame::SpriteModels' Polygons). */
		SpriteModel,
	};

	/** One entry of the painter's order. */
	struct FFieldDrawItem
	{
		uint32 Index = 0;
		EFieldDrawKind Kind = EFieldDrawKind::Scenery;
		/** A gear's polygon or cloth triangle, in FFieldFrame::Gears. */
		EFieldGearDraw Gear = EFieldGearDraw::None;
	};

	/** A placed model whose vertices a MIMe animation moves: the tick's vertices, in FFieldFrame::Vertices from First. */
	struct FFieldMovedModel
	{
		uint32 Object = 0;
		uint32 First = 0;
		uint32 Count = 0;
	};

	/** A placed model whose texture coordinates the scripts scroll: every corner samples ((u + U) mod 256, (v + V) mod 256). */
	struct FFieldScrolledModel
	{
		uint32 Object = 0;
		uint8 U = 0;
		uint8 V = 0;
	};

	/**
	 * The picture of the screen as the frame draws it (FFieldCover): over everything else, or in a
	 * map change's fade frames between the scripts' fade layer and the map's.
	 */
	struct FFieldFrameCover
	{
		bool bShown = false;
		/** Added to what lies below rather than opaque (an opaque picture's black pixels let it through). */
		bool bAdditive = false;
		/**
		 * The picture is the frame shown last when the picture was taken; otherwise the columns are
		 * drawn from VRAM's picture area, in the 2D layer.
		 */
		bool bScreen = true;
		/** The colour its texels are modulated by: 128 leaves them as they are. */
		uint8 Brightness = 0;
		/** 4.12 about the screen's centre. */
		int32 Scale = 0;
		/** The count of pictures taken of the screen when this one was. */
		int32 Taken = 0;
		/** The 2D layer's primitives drawn before the picture; the rest are drawn over it. */
		uint32 LayerIndex = 0;
	};

	/**
	 * What the renderer reads of one field tick, as the tick drew it: the view, the scenery (each
	 * placed model's matrix, each polygon's colours and projection, the vertices MIMe animations
	 * moved and the texture coordinates the scripts scrolled), the characters' sprites and shadows
	 * and the particles as view-space quads, the gears' models, all of it in the original's painter's order, the screen
	 * wave, the screen's 2D layer (the cockpit's instruments, the fade layers, the message windows,
	 * the screen markers, Select's picture) and the picture of the screen over it. Continuous
	 * quantities blend between two ticks' frames by their keys (the scenery by its models, the moved
	 * vertices by their models, the characters by FFieldCharacterView, the particles by theirs, the
	 * gears by their layout, the wave by its cells, the 2D layer by its primitives' blend keys); cels, palettes and textures are
	 * the latest tick's. Built into the same memory tick after tick: it allocates only while it grows.
	 */
	struct FFieldFrame
	{
		static constexpr std::size_t WaveCells = FFieldScreenWave::Columns * FFieldScreenWave::Rows;

		/** Counts the field's ticks since the map was entered: frames of consecutive ticks blend. */
		uint32 Tick = 0;
		/** False while the map settles, and in frames drawn without the field (a fade's, Select's picture): the screen is cleared to black and only the 2D layer drawn. */
		bool bSceneDrawn = false;
		/** The colour the screen is cleared to before the scene. */
		FMdlColor Background{};
		/** The rectangle of the 320 x 224 screen the scene and the 2D layer are clipped to; the clear colour fills the whole display. */
		FVramRect DrawClip{0, 0, FFieldScreenWave::ScreenWidth, FFieldScreenWave::ScreenHeight};
		/** The button glyphs in the 2D layer's message text, which a display shows as the player's buttons. */
		std::vector<FTextButtonCell> TextButtons;
		/**
		 * Where the message windows' primitives begin and end in the 2D layer: a display may draw that stretch at a scale of
		 * its own, so the boxes are not stretched by the picture's fraction of a display pixel a pixel (empty when no window
		 * is open).
		 */
		std::array<uint32, 2> MessageRange{};
		/** The matrices the tick drew with, and the player's offsets they show. */
		FFieldView View;
		FFieldViewOffsets ShownOffsets;
		/** View.View without the whole steps the game's look-at takes, which a display draws with where it is known. */
		FAffine UnroundedView;
		bool bUnrounded = false;
		/** The camera's eye and target (whole units), which tell a cut from a move. */
		std::array<int32, 3> Eye{};
		std::array<int32, 3> Target{};
		/** The scenery as the tick drew it, and each placed model's place in the world (whole units). */
		FFieldSceneFrame Scenery;
		std::vector<std::array<int32, 3>> SceneryWorld;
		/** The models MIMe animations moved, and their vertices as the tick left them. */
		std::vector<FFieldMovedModel> MovedModels;
		std::vector<FGteVector> Vertices;
		std::vector<FFieldScrolledModel> ScrolledModels;
		std::vector<FFieldCharacterView> Characters;
		std::vector<FFieldCharacterQuad> Quads;
		/**
		 * The gears' layout slots (FFieldGearLayoutItem::Slot) whose models stand in for the party's sprites outside
		 * cutscenes: the scenery in front of them may be seen through about them, as about a member's sprite.
		 */
		std::vector<int32> SeenThroughGearSlots;
		std::vector<FFieldParticleQuad> Particles;
		FFieldBackdrop Backdrop;
		/** The gears' models as the tick drew them. */
		FFieldGearsFrame Gears;
		/** The sprites' spawned children's models as the tick drew them. */
		FFieldSpriteModelsFrame SpriteModels;
		/**
		 * The scene in the order the GPU draws it: the background table, then the main one, far to
		 * near; within an entry the gears, the particles, the characters, the children's models, then
		 * the scenery, each the last linked first (the panorama before the background table's scenery
		 * of its entry).
		 */
		std::vector<FFieldDrawItem> Order;
		/**
		 * The screen wave: the scene drawn before the Order's first item in WaveBucket or nearer is
		 * drawn again through WaveCells (row after row, on the 320 x 224 screen), and the rest over it.
		 * A view wider than the screen draws it through WideWave instead (FFieldScreenWave::BuildWideCells).
		 */
		bool bScreenWave = false;
		std::array<FFieldScreenWaveCell, WaveCells> Wave{};
		std::vector<FFieldScreenWaveCell> WideWave;
		/** The 2D layer over the scene, on the 320 x 224 screen; the fades cover the whole display. */
		FDrawList Screen;
		FFieldFrameCover Cover;
		/** Where VRAM holds pixels of a picture of the screen, which the renderer puts there. */
		std::vector<FFieldScreenInVram> ScreenInVram;
	};

	/** The main table's entry the screen wave's cells are linked into: what lies nearer is drawn over the wave. */
	inline constexpr int32 FieldWaveBucket = 1;
}
