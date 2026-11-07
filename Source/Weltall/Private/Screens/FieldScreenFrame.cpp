#include "FieldScreenFrame.h"

#include "Field/FieldRenderState.h"
#include "Presentation/ScreenLayout.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/FieldPictureRendering.h"
#include "Rendering/FieldScreenUnitsRendering.h"
#include "Rendering/MovieRendering.h"
#include "Rendering/VramTexture.h"

namespace
{
	/** The field's screen, which its 2D layer is placed on. */
	constexpr Weltall::FScreenSize FieldScreen{320, 224};
	constexpr float NeutralBrightness = 128.0f;
}

TSharedRef<FFieldCoverPicture> FFieldCoverPicture::Make(TSharedPtr<const IScreenFrame> Shown)
{
	FFieldCoverPicture* Made = new FFieldCoverPicture();
	Made->Shown = MoveTemp(Shown);
	return TSharedRef<FFieldCoverPicture>(Made, [](FFieldCoverPicture* Released)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseFieldCoverPicture)([Released](FRHICommandListImmediate&) { delete Released; });
	});
}

FRDGTextureRef FFieldCoverPicture::Texture(FRDGBuilder& GraphBuilder, const FScreenArea& Area)
{
	return Draw(GraphBuilder, Area.At(FIntPoint::ZeroValue), Drawn);
}

FRDGTextureRef FFieldCoverPicture::ScreenPixels(FRDGBuilder& GraphBuilder)
{
	return Draw(GraphBuilder, FScreenArea(FIntRect(0, 0, FieldScreen.Width, FieldScreen.Height)), DrawnAtScreen);
}

FRDGTextureRef FFieldCoverPicture::Draw(FRDGBuilder& GraphBuilder, const FScreenArea& Area, FDrawn& Into)
{
	const FIntPoint Size = Area.Size();
	if (Into.Target.IsValid() && Into.Target->GetDesc().Extent == Size && Into.WholeScale == Area.WholeScale)
	{
		return GraphBuilder.RegisterExternalTexture(Into.Target);
	}
	if (!Shown.IsValid())
	{
		return nullptr;
	}
	FRDGTextureRef Picture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Size, PF_B8G8R8A8, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("FieldCoverPicture"));
	AddClearRenderTargetPass(GraphBuilder, Picture, FLinearColor::Black);
	Shown->AddPasses(GraphBuilder, Picture, Area);
	GraphBuilder.QueueTextureExtraction(Picture, &Into.Target);
	Into.WholeScale = Area.WholeScale;
	return Picture;
}

TSharedRef<FFieldStillFrame> FFieldStillFrame::Make(TSharedRef<const IScreenFrame> Frame)
{
	return TSharedRef<FFieldStillFrame>(new FFieldStillFrame(MoveTemp(Frame)), [](FFieldStillFrame* Released)
	{
		ENQUEUE_RENDER_COMMAND(ReleaseFieldStillFrame)([Released](FRHICommandListImmediate&) { delete Released; });
	});
}

void FFieldStillFrame::AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FScreenArea& Area) const
{
	const FRDGTextureDesc& Desc = Output->Desc;
	if (Drawn.IsValid() && Drawn->GetDesc().Extent == Desc.Extent && Drawn->GetDesc().Format == Desc.Format && DrawnArea == Area)
	{
		AddCopyTexturePass(GraphBuilder, GraphBuilder.RegisterExternalTexture(Drawn), Output);
		return;
	}
	FRDGTextureRef Picture = GraphBuilder.CreateTexture(
		FRDGTextureDesc::Create2D(Desc.Extent, Desc.Format, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("FieldStillPicture"));
	AddClearRenderTargetPass(GraphBuilder, Picture, FLinearColor::Black);
	Frame->AddPasses(GraphBuilder, Picture, Area);
	GraphBuilder.QueueTextureExtraction(Picture, &Drawn);
	DrawnArea = Area;
	AddCopyTexturePass(GraphBuilder, Picture, Output);
}

void WriteScreenIntoVram(const TSharedRef<FVramTexture>& Vram, const std::vector<Weltall::FFieldScreenInVram>& Written,
	const TMap<int32, TSharedPtr<FFieldCoverPicture>>& Pictures)
{
	static std::vector<Weltall::FVramRect> Exclusions;
	Exclusions.clear();
	for (const auto& Region : Written) Exclusions.push_back(Region.Rect);
	Vram->SetReplacementExclusions(Exclusions);
	struct FWrite
	{
		FFieldScreenUnits Units;
		TSharedPtr<FFieldCoverPicture> Picture;
	};
	TArray<FWrite> Writes;
	for (const Weltall::FFieldScreenInVram& Region : Written)
	{
		FWrite& Write = Writes.AddDefaulted_GetRef();
		Write.Units.Rect = FIntRect(Region.Rect.X, Region.Rect.Y, Region.Rect.X + Region.Rect.Width, Region.Rect.Y + Region.Rect.Height);
		Write.Units.ScreenOffset = FIntPoint(Region.ScreenX, Region.ScreenY);
		Write.Units.Brightness = Region.Brightness;
		if (const TSharedPtr<FFieldCoverPicture>* Found = Pictures.Find(Region.Taken))
		{
			Write.Picture = *Found;
		}
	}
	if (Writes.IsEmpty())
	{
		return;
	}
	ENQUEUE_RENDER_COMMAND(WriteFieldScreenIntoVram)([Vram, Writes = MoveTemp(Writes)](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef Units = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Vram->Texture(), TEXT("PlayStationVram")));
		for (const FWrite& Write : Writes)
		{
			FRDGTextureRef Source = Write.Picture.IsValid() ? Write.Picture->ScreenPixels(GraphBuilder) : nullptr;
			AddFieldScreenUnitsPass(GraphBuilder, Source, Units, Write.Units);
		}
		GraphBuilder.SetTextureAccessFinal(Units, ERHIAccess::SRVMask);
		GraphBuilder.Execute();
	});
}

FFieldScreenFrame::FFieldScreenFrame(TSharedRef<const FFieldSceneMesh> InMesh, TSharedRef<FVramTexture> InVram)
	: Mesh(MoveTemp(InMesh)), Vram(MoveTemp(InVram))
{
}

void FFieldScreenFrame::AddPasses(FRDGBuilder& GraphBuilder, FRDGTextureRef Output, const FScreenArea& Area) const
{
	const bool bScreenPicture = Cover.bShown && Cover.bScreen && Picture.IsValid();
	FRDGTextureRef PictureTexture = bScreenPicture ? Picture->Texture(GraphBuilder, Area) : nullptr;
	const FIntRect ScreenRect = Area.ScreenRect(FieldScreen);
	const std::size_t CoverAt = FMath::Min<std::size_t>(Cover.LayerIndex, Layer.Primitives.size());
	// The message windows at a whole number of display pixels a pixel of their own, centred and standing on the bottom of
	// the area the picture would have them in: their letters stay even rather than stretched by the picture's fraction of
	// a pixel, and the rest of the screen keeps the scale it had.
	const std::size_t WindowFirst = FMath::Min<std::size_t>(MessageRange[0], CoverAt);
	const std::size_t WindowLast = FMath::Min<std::size_t>(MessageRange[1], CoverAt);
	FIntRect WindowRect = ScreenRect;
	bool bOwnWindowScale = false;
	if (MessageBoxScale != Weltall::EMessageBoxScale::Original && WindowLast > WindowFirst)
	{
		const double PictureScale = Area.Layout(FieldScreen).Scale();
		const int32 Whole = FMath::Max(1, FMath::FloorToInt(PictureScale));
		const int32 Chosen = FMath::Max(1, MessageBoxScale == Weltall::EMessageBoxScale::Smaller ? Whole - 1 : Whole);
		// Nothing to do where the picture is already drawn at whole pixels.
		if (Chosen != FMath::RoundToInt32(PictureScale))
		{
			const int32 Width = FieldScreen.Width * Chosen;
			const int32 Height = FieldScreen.Height * Chosen;
			const FIntPoint Min(ScreenRect.Min.X + (ScreenRect.Width() - Width) / 2, ScreenRect.Max.Y - Height);
			WindowRect = FIntRect(Min, Min + FIntPoint(Width, Height));
			bOwnWindowScale = true;
		}
	}
	if (MoviePicture.IsValid())
	{
		const Weltall::FScreenRect WholeScreen{0, 0, static_cast<int16>(FieldScreen.Width), static_cast<int16>(FieldScreen.Height)};
		AddMoviePasses(GraphBuilder, *MoviePicture, Output, ScreenRect, FieldScreen, WholeScreen);
	}
	else
	{
		AddFieldScenePasses(GraphBuilder, *Mesh, *Vram, Draw, Output, Area.Rows(FieldScreen));
	}
	FDrawListFrame Frame{&Layer, bPreviousLayer ? &PreviousLayer : nullptr, bPreviousLayer ? &Matches : nullptr, Alpha};
	const auto AddLayer = [&](const FIntRect& Rect, std::size_t First, std::size_t End)
	{
		Frame.First = First;
		Frame.End = End;
		if (IconVram.IsValid())
		{
			AddIconDrawListPasses(GraphBuilder, *Vram, *IconVram, Frame, Output, Rect, FieldScreen);
		}
		else
		{
			AddDrawListPasses(GraphBuilder, *Vram, Frame, Output, Rect, FieldScreen);
		}
	};
	if (bOwnWindowScale)
	{
		AddLayer(ScreenRect, 0, WindowFirst);
		AddLayer(WindowRect, WindowFirst, WindowLast);
		AddLayer(ScreenRect, WindowLast, CoverAt);
	}
	else
	{
		AddLayer(ScreenRect, 0, CoverAt);
	}
	if (PictureTexture != nullptr)
	{
		// The picture covers the whole area it was drawn at, scaled about its centre.
		const float Scale = static_cast<float>(Cover.Scale) / Weltall::FFieldCover::UnitScale;
		const FVector2f Centre = FVector2f(Area.Min) + FVector2f(Area.Size()) * 0.5f;
		const FVector2f Half = FVector2f(Area.Size()) * 0.5f * Scale;
		const FVector4f Corners[4] = {FVector4f(-1.0f, -1.0f, 0.0f, 0.0f), FVector4f(1.0f, -1.0f, 1.0f, 0.0f), FVector4f(-1.0f, 1.0f, 0.0f, 1.0f), FVector4f(1.0f, 1.0f, 1.0f, 1.0f)};
		TArray<FVector4f, TInlineAllocator<6>> Quad;
		for (const int32 Corner : {0, 1, 2, 1, 3, 2})
		{
			Quad.Add(Corners[Corner]);
		}
		FFieldPictureDraw Drawn;
		Drawn.Vertices = Quad;
		Drawn.Placement = FVector4f(Half.X, Half.Y, Centre.X, Centre.Y);
		Drawn.ColorScale = static_cast<float>(Cover.Brightness) / NeutralBrightness;
		Drawn.Blend = Cover.bAdditive ? EFieldPictureBlend::Add : EFieldPictureBlend::OpaqueBlackTransparent;
		Drawn.bScissor = true;
		Drawn.Scissor = Area;
		AddFieldPicturePass(GraphBuilder, PictureTexture, Output, Drawn);
	}
	AddLayer(ScreenRect, CoverAt, Layer.Primitives.size());
}
