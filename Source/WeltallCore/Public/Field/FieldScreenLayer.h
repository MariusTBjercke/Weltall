#pragma once

#include "Foundation/Types.h"
#include "Graphics/Vram.h"
#include "Input/ButtonGlyphs.h"

#include <array>
#include <vector>

namespace Weltall
{
	struct FDrawList;
	struct FEventContext;
	struct FFieldCompassFrame;
	struct FFieldDrawingTables;
	struct FFieldMessages;
	enum class EFieldFrame : uint8;

	/**
	 * The lines of text the open message windows show, typed into VRAM as the kernel types them: each
	 * window's lines in its own place (two lines a strip of 13 rows, one in each half of the 4-bit
	 * texels), glyph over glyph, read by the windows' CLUTs for even and odd lines. Scratch is kept
	 * from call to call.
	 */
	WELTALLCORE_API void TypeFieldMessageText(const FFieldMessages& Messages, const FFieldDrawingTables& Tables, FVram& Vram, FVramImage& Scratch);

	/**
	 * The screen's 2D layer over the scene as the field draws a frame of the given kind. A tick's: the
	 * cockpit's instruments, the compass (when given), the scripts' fade layer, the message windows (each its background,
	 * choice cursor, portrait, frame, "more" cursor and text, the frontmost last), the map's fade
	 * layer over them all, then the screen markers. A map change's fade frame: the scripts' fade layer
	 * and the map's. Select's picture's frame: the picture alone. The picture of the screen goes where
	 * the returned index says, among the primitives when it is drawn from VRAM. The fades are
	 * full-screen tiles that cover the whole display whatever its width; the rest is placed on the
	 * 320 x 224 screen, and all of it clipped to the scripts' drawing area when they narrow it.
	 * Primitives that move or change between ticks carry blend keys. Layer's memory is reused. Each
	 * window is shown WindowShifts' pixels (when given) from where the game placed it. With
	 * bSolidMessageBox a window's background is opaque black rather than the kernel's subtract, which
	 * darkens what lies behind it; MessageRange, when given, takes where the windows' primitives begin
	 * and end, so a display can draw them at a scale of its own.
	 */
	WELTALLCORE_API uint32 BuildFieldScreenLayer(const FEventContext& Context, const FFieldDrawingTables& Tables, EFieldFrame Kind, FDrawList& Layer,
		const FFieldCompassFrame* Compass = nullptr, const std::array<std::array<int16, 2>, 4>* WindowShifts = nullptr,
		std::vector<FTextButtonCell>* TextButtons = nullptr, bool bSolidMessageBox = false, std::array<uint32, 2>* MessageRange = nullptr);
}
