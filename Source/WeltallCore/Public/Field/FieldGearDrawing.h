#pragma once

#include "Field/FieldCamera.h"
#include "Field/FieldSceneFrame.h"
#include "Foundation/Result.h"
#include "Foundation/Types.h"
#include "Gears/GearCloth.h"
#include "Graphics/AffineBlend.h"
#include "Graphics/Gte.h"
#include "Graphics/MdlModel.h"
#include "Graphics/TexturePictures.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace Weltall
{
	class FGearModels;
	class IGearDress;
	struct FFieldGearLights;
	struct FGearBonePackets;
	struct FGearModel;

	/**
	 * The gears' floor shadow as each model's load sets it up: the sprites' shadow blob (a 4-bit page
	 * at (640, 256) drawn subtractive, its CLUT at (256, 243)) on a semi-transparent quad of colour 40h,
	 * its corners (+x, +z), (-x, +z), (+x, -z), (-x, -z) of the model's half-sizes taking the texels'
	 * first column and row, the last column, the last row, and both.
	 */
	namespace FieldGearShadow
	{
		constexpr uint16 TexturePage = 0x5A;
		constexpr uint16 Clut = 0x3CD0;
		constexpr uint8 Code = 0x2E;
		constexpr uint8 Shade = 0x40;
		constexpr uint8 FirstTexel = 0x00;
		constexpr uint8 LastTexel = 0x0F;
		constexpr uint8 FirstRow = 0xE0;
		constexpr uint8 LastRow = 0xEF;
	}

	/** The cloths' culling under the scenery's: the original's rules, or whatever a view of any width shows. */
	constexpr EGearClothCulling GearClothCulling(EFieldCulling Culling)
	{
		return Culling == EFieldCulling::Original ? EGearClothCulling::Original : EGearClothCulling::InRange;
	}

	/** Something a loaded gear draws, in the order the frame lays them out: each loaded slot's floor shadow, then each of its bones' parts. */
	struct FFieldGearLayoutItem
	{
		int32 Slot = 0;
		/** The bone's part as the load built it; null for the floor shadow. */
		const FGearBonePackets* Packets = nullptr;
		const FMdlPart* Part = nullptr;
		/** The shadow's half-sizes, across and along. */
		int16 HalfWidth = 0;
		int16 HalfDepth = 0;
		/** Its polygons in the frame's Polygons: the shadow's one quad, or the part's primitives. */
		uint32 FirstPolygon = 0;
		uint32 PolygonCount = 0;
		/** Its model's textures lie in VRAM's port rows: its pages and CLUTs are read with their high y bits. */
		bool bPortRows = false;
	};

	/**
	 * What a layout's parts point into: the models it was laid out from, and the drawings that stand in
	 * for them. A layout outlives the module that laid it out, which frees a slot's model as its scripts
	 * ask, so a frame keeps one beside its items for as long as anything may read them: the renderer
	 * takes a picture of the frame before the latest, whose parts it reads again.
	 */
	struct FFieldGearLayoutSources
	{
		std::vector<std::shared_ptr<const FMdlModel>> Parts;
		std::vector<std::shared_ptr<const IGearDress>> Dresses;

		/** Keeps Model's parts, and the drawing that stands in for them, alongside this. */
		WELTALLCORE_API void Add(const FGearModel& Model);
		void Clear() { Parts.clear(); Dresses.clear(); }
	};

	/** The field's ordering table takes a quarter of the depths: the kernel's shift for the gears' buckets. */
	inline constexpr int32 FieldGearOrderingShift = 2;

	/**
	 * Lays out what the loaded gears draw, one item per frame object, into Items (reusing its memory). The
	 * items' parts point into the models: Sources, when given, keeps those for as long as it lives.
	 */
	WELTALLCORE_API void LayOutFieldGears(const FGearModels& Models, std::vector<FFieldGearLayoutItem>& Items,
		FFieldGearLayoutSources* Sources = nullptr);

	/** Adds another module's models to a layout, after what it holds, their slots numbered from FirstSlot on. */
	WELTALLCORE_API void AddFieldGearLayout(const FGearModels& Models, int32 FirstSlot, std::vector<FFieldGearLayoutItem>& Items,
		FFieldGearLayoutSources* Sources = nullptr);

	/** A floor shadow or a bone's part as a tick drew it. */
	struct FFieldGearObject
	{
		/** Model to view space, as the GTE drew it (a billboarded part's rotation as the drawing sets it). */
		FGteMatrix Matrix;
		/** Its model's place in the world, whole units: a model that jumped between ticks is not drawn in between. */
		std::array<int32, 3> World{};
		bool bDrawn = false;
		/**
		 * Matrix as real numbers, for a model its host places in the world (FGearModel::Placement) finer than the
		 * GTE's entries hold: what its corners go through, while Matrix did the testing and the sorting.
		 */
		bool bFine = false;
		FAffine Fine;
	};

	/** The space a placed model's cloths lie in, under the view: their points go through it rather than through the view alone. */
	struct FFieldGearClothSpace
	{
		uint8 Slot = 0;
		FAffine Space;
	};

	/** A polygon of the layout as a tick drew it. */
	struct FFieldGearPolygon
	{
		/** Its corners' colours, one per corner for the Gouraud kinds: as built, or lit again. */
		std::array<FMdlColor, 4> Colors{};
		/** The ordering-table entry it is linked into. */
		uint16 Bucket = 0;
		/** FieldPolygonDrop bits: zero when the original draws it (the floor shadow is never dropped). */
		uint8 Dropped = 0;
		/** Drawn under the rules the frame was evaluated with. */
		bool bDrawn = false;
	};

	/** What the gears link into the ordering table. */
	enum class EFieldGearDraw : uint8
	{
		None,
		/** A polygon of the layout: a part's, or a floor shadow. */
		Polygon,
		/** A triangle of FFieldGearsFrame::Cloths. */
		Cloth,
	};

	struct FFieldGearLink
	{
		EFieldGearDraw Kind = EFieldGearDraw::None;
		uint32 Index = 0;
		uint16 Bucket = 0;
	};

	/**
	 * What the field's gears draw in one tick: every object and polygon of the layout (drawn or not),
	 * the cloths' triangles drawn, and what the drawing linked into the ordering table, in the order
	 * it linked them (within an entry the last is drawn first). A model its host places (FGearModel::Placement) is
	 * tested and sorted through its matrices put there in the GTE's whole numbers, and drawn through the same as
	 * real numbers; its floor shadow is its host's to draw. Kept from tick to tick: it allocates only while it
	 * grows.
	 */
	struct FFieldGearsFrame
	{
		/** The gears' models' revision the layout is of: objects and polygons pair from tick to tick only under the same. */
		uint32 Layout = 0;
		std::vector<FFieldGearObject> Objects;
		std::vector<FFieldGearPolygon> Polygons;
		std::vector<FGearDrawnClothTriangle> Cloths;
		/** One for each placed model that draws a cloth; any other model's cloths lie in the world. */
		std::vector<FFieldGearClothSpace> ClothSpaces;
		std::vector<FFieldGearLink> Links;
		/** The texture regions of the laid-out models with their loads' colours, each model's once (FGearModel::TextureSeeds). */
		std::vector<FTexturePictureSeed> TextureSeeds;
		/** Scratch: the layout, and a part's vertices projected. */
		std::vector<FFieldGearLayoutItem> Items;
		std::vector<FGteProjection> Projected;
		/** The models the items point into, kept while this frame may still be read. */
		FFieldGearLayoutSources Sources;
	};

	/**
	 * The gears' drawing of a tick as the field draws them (after its own logic: the models' tick has
	 * run): each loaded model's floor shadow, its shown bones' parts relit through the gears' lights
	 * turned by the root and the bone, and its cloths, under the view, sorted as the kernel sorts
	 * them; laid out but not drawn when bDrawn is false (the field draws its gears only while it
	 * counts some). A cloth drawn before any part is lit by LightBefore, the light matrix the drawing
	 * before the gears left in the GTE; without one it fails there. The buckets are the depths shifted
	 * by OrderingShift (the gear shop's table takes them whole).
	 */
	[[nodiscard]] WELTALLCORE_API std::optional<FError> EvaluateFieldGears(const FGearModels& Models, const FFieldView& View, const FFieldGearLights& Lights,
		int32 FarBucket, EFieldCulling Culling, bool bDrawn, FFieldGearsFrame& Frame, int32 OrderingShift = FieldGearOrderingShift,
		const FGteMatrix* LightBefore = nullptr);

	/**
	 * Another module's models drawn into a frame EvaluateFieldGears made, after its own and under the same view and
	 * lights, their slots numbered from FirstSlot on. Tints gives each of the module's slots a sprite's tint (128
	 * neutral) its lit colours are drawn under: models that stand in for sprites take the sprites' tints.
	 */
	[[nodiscard]] WELTALLCORE_API std::optional<FError> AddFieldGears(const FGearModels& Models, int32 FirstSlot, std::span<const std::array<uint8, 3>> Tints,
		const FFieldView& View, const FFieldGearLights& Lights, int32 FarBucket, EFieldCulling Culling, bool bDrawn, FFieldGearsFrame& Frame,
		int32 OrderingShift = FieldGearOrderingShift, const FGteMatrix* LightBefore = nullptr);
}
