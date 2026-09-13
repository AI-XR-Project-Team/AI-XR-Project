#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
// 개발 보조: WBP_DocentChat 의 위젯 트리를 JSON 으로 덤프하고 오프스크린으로
// 그려 PNG 로 남긴다. 에디터를 열지 않고 스캔 화면 배치를 확인하는 용도.
#include "Misc/AutomationTest.h"
#include "../DocentChatWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "AssetCompilingManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScanUiDumpTest, "TimeMachineAR.Docent.ScanPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static FString BrushJson(const FSlateBrush& B)
{
	const FLinearColor T = B.TintColor.GetSpecifiedColor();
	return FString::Printf(TEXT("{\"res\":\"%s\",\"size\":[%.0f,%.0f],\"draw\":%d,\"tint\":[%.2f,%.2f,%.2f,%.2f],\"round\":%d,\"radii\":[%.0f,%.0f,%.0f,%.0f],\"outline\":%.1f}"),
		*GetPathNameSafe(B.GetResourceObject()), B.ImageSize.X, B.ImageSize.Y, (int32)B.DrawAs,
		T.R, T.G, T.B, T.A, (int32)B.OutlineSettings.RoundingType,
		B.OutlineSettings.CornerRadii.X, B.OutlineSettings.CornerRadii.Y, B.OutlineSettings.CornerRadii.Z, B.OutlineSettings.CornerRadii.W,
		B.OutlineSettings.Width);
}

static void DumpWidget(UWidget* W, int32 Depth, TArray<FString>& Out)
{
	if (!W) { return; }
	FString Line = FString::Printf(TEXT("{\"d\":%d,\"name\":\"%s\",\"class\":\"%s\",\"vis\":%d"),
		Depth, *W->GetName(), *W->GetClass()->GetName(), (int32)W->GetVisibility());
	if (UCanvasPanelSlot* S = Cast<UCanvasPanelSlot>(W->Slot))
	{
		const FAnchorData& L = S->GetLayout();
		Line += FString::Printf(TEXT(",\"canvas\":{\"anchors\":[%.2f,%.2f,%.2f,%.2f],\"off\":[%.0f,%.0f,%.0f,%.0f],\"align\":[%.2f,%.2f],\"auto\":%d,\"z\":%d}"),
			L.Anchors.Minimum.X, L.Anchors.Minimum.Y, L.Anchors.Maximum.X, L.Anchors.Maximum.Y,
			L.Offsets.Left, L.Offsets.Top, L.Offsets.Right, L.Offsets.Bottom, L.Alignment.X, L.Alignment.Y, S->GetAutoSize() ? 1 : 0, S->GetZOrder());
	}
	else if (UOverlaySlot* OS = Cast<UOverlaySlot>(W->Slot))
	{
		const FMargin P = OS->GetPadding();
		Line += FString::Printf(TEXT(",\"overlay\":{\"pad\":[%.0f,%.0f,%.0f,%.0f],\"h\":%d,\"v\":%d}"), P.Left, P.Top, P.Right, P.Bottom, (int32)OS->GetHorizontalAlignment(), (int32)OS->GetVerticalAlignment());
	}
	else if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(W->Slot))
	{
		const FMargin P = HS->GetPadding();
		Line += FString::Printf(TEXT(",\"hbox\":{\"pad\":[%.0f,%.0f,%.0f,%.0f],\"h\":%d,\"v\":%d,\"size\":\"%s %.1f\"}"), P.Left, P.Top, P.Right, P.Bottom, (int32)HS->GetHorizontalAlignment(), (int32)HS->GetVerticalAlignment(), HS->GetSize().SizeRule == ESlateSizeRule::Fill ? TEXT("fill") : TEXT("auto"), HS->GetSize().Value);
	}
	else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(W->Slot))
	{
		const FMargin P = VS->GetPadding();
		Line += FString::Printf(TEXT(",\"vbox\":{\"pad\":[%.0f,%.0f,%.0f,%.0f],\"h\":%d,\"v\":%d,\"size\":\"%s %.1f\"}"), P.Left, P.Top, P.Right, P.Bottom, (int32)VS->GetHorizontalAlignment(), (int32)VS->GetVerticalAlignment(), VS->GetSize().SizeRule == ESlateSizeRule::Fill ? TEXT("fill") : TEXT("auto"), VS->GetSize().Value);
	}
	else if (W->Slot)
	{
		Line += FString::Printf(TEXT(",\"slot\":\"%s\""), *W->Slot->GetClass()->GetName());
	}
	if (USizeBox* SB = Cast<USizeBox>(W))
	{
		Line += FString::Printf(TEXT(",\"sizebox\":[%.0f,%.0f,%.0f,%.0f]"), SB->GetWidthOverride(), SB->GetHeightOverride(), SB->GetMinDesiredWidth(), SB->GetMinDesiredHeight());
	}
	if (UImage* I = Cast<UImage>(W))
	{
		const FLinearColor C = I->GetColorAndOpacity();
		Line += FString::Printf(TEXT(",\"brush\":%s,\"color\":[%.2f,%.2f,%.2f,%.2f]"), *BrushJson(I->GetBrush()), C.R, C.G, C.B, C.A);
	}
	if (UBorder* B = Cast<UBorder>(W))
	{
		const FLinearColor C = B->GetBrushColor();
		const FMargin P = B->GetPadding();
		Line += FString::Printf(TEXT(",\"brush\":%s,\"color\":[%.2f,%.2f,%.2f,%.2f],\"pad\":[%.0f,%.0f,%.0f,%.0f],\"h\":%d,\"v\":%d"), *BrushJson(B->Background), C.R, C.G, C.B, C.A, P.Left, P.Top, P.Right, P.Bottom, (int32)B->GetHorizontalAlignment(), (int32)B->GetVerticalAlignment());
	}
	if (UButton* B = Cast<UButton>(W))
	{
		const FButtonStyle& St = B->GetStyle();
		const FMargin P = St.NormalPadding;
		Line += FString::Printf(TEXT(",\"btn_normal\":%s,\"btn_pad\":[%.0f,%.0f,%.0f,%.0f],\"enabled\":%d"), *BrushJson(St.Normal), P.Left, P.Top, P.Right, P.Bottom, B->GetIsEnabled() ? 1 : 0);
	}
	if (UTextBlock* T = Cast<UTextBlock>(W))
	{
		const FLinearColor C = T->GetColorAndOpacity().GetSpecifiedColor();
		Line += FString::Printf(TEXT(",\"text\":\"%s\",\"font\":\"%s %d\",\"color\":[%.2f,%.2f,%.2f,%.2f]"), *T->GetText().ToString().ReplaceCharWithEscapedChar(), *GetNameSafe(T->GetFont().FontObject), T->GetFont().Size, C.R, C.G, C.B, C.A);
	}
	const FWidgetTransform& RT = W->GetRenderTransform();
	if (!RT.Translation.IsNearlyZero() || !RT.Scale.Equals(FVector2D(1, 1)) || W->GetRenderOpacity() != 1.f)
	{
		Line += FString::Printf(TEXT(",\"rt\":[%.0f,%.0f,%.2f,%.2f],\"opacity\":%.2f"), RT.Translation.X, RT.Translation.Y, RT.Scale.X, RT.Scale.Y, W->GetRenderOpacity());
	}
	Line += TEXT("}");
	Out.Add(Line);
	if (UPanelWidget* P = Cast<UPanelWidget>(W))
	{
		for (int32 i = 0; i < P->GetChildrenCount(); ++i) { DumpWidget(P->GetChildAt(i), Depth + 1, Out); }
	}
}

bool FScanUiDumpTest::RunTest(const FString& Parameters)
{
	UWorld* World = nullptr;
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
		if (Context.WorldType == EWorldType::Editor) { World = Context.World(); break; }
	if (!TestNotNull(TEXT("Editor world"), World)) { return false; }
	UClass* Class = LoadClass<UDocentChatWidget>(nullptr, TEXT("/Game/UI/Docent/WBP_DocentChat.WBP_DocentChat_C"));
	if (!TestNotNull(TEXT("Docent chat Blueprint"), Class)) { return false; }
	// 에디터 월드에는 게임 인스턴스가 없어 도슨트 클라이언트를 못 찾는다. 이 테스트의 관심사가 아니다.
	AddExpectedError(TEXT("DocentClient"), EAutomationExpectedErrorFlags::Contains, 0);
	UDocentChatWidget* Widget = CreateWidget<UDocentChatWidget>(World, Class);
	if (!TestNotNull(TEXT("Docent chat instance"), Widget)) { return false; }
	Widget->AddToRoot();
	FAssetCompilingManager::Get().FinishAllCompilation();
	FWidgetRenderer Renderer(false);
	TSharedRef<SWidget> SlateWidget = Widget->TakeWidget();
	const int32 W = 1440, H = 3120;
	UTextureRenderTarget2D* Target = NewObject<UTextureRenderTarget2D>();
	Target->InitCustomFormat(W, H, PF_FloatRGBA, true);
	Target->UpdateResourceImmediate();
	// 실기기 DPI 스케일. UIScaleCurve(ShortestSide) 로 1440 폭이면 1.333 이다.
	Renderer.DrawWidget(Target, SlateWidget, 1.333f, FVector2D(W, H), 0.f);
	Renderer.DrawWidget(Target, SlateWidget, 1.333f, FVector2D(W, H), 0.f);
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags Flags(RCM_UNorm); Flags.SetLinearToGamma(true);
	bool bSaved = Target && Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels, Flags);
	TArray64<uint8> Png;
	if (bSaved) { FImageUtils::PNGCompressImageArray(W, H, Pixels, Png); }
	if (bSaved) { bSaved = FFileHelper::SaveArrayToFile(Png, *(FPaths::ProjectSavedDir() / TEXT("ScanUiPreview.png"))); }
	TestTrue(TEXT("Rendered scan preview"), bSaved);
	TArray<FString> Lines;
	DumpWidget(Widget->WidgetTree ? Widget->WidgetTree->RootWidget.Get() : nullptr, 0, Lines);
	FFileHelper::SaveStringArrayToFile(Lines, *(FPaths::ProjectSavedDir() / TEXT("ScanUiTree.jsonl")));
	Widget->RemoveFromRoot();
	return bSaved;
}
#endif
