#include "NavFullMapWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "NavDestButton.h"
#include "NavDestinations.h"

namespace
{
 constexpr float ReferenceWidth = 941.f;
 constexpr float ReferenceHeight = 1672.f;
 constexpr float TopCrop = 0.f;
 struct FReferenceHotspot { int32 Number; FVector2D Center; FVector2D Size; };
 // Source-image coordinates identify artwork only; navigation uses the live node ID.
 const FReferenceHotspot Hotspots[] = {
  {1,{160,1110},{110,110}}, {2,{151,889},{110,120}}, {3,{610,997},{110,110}},
  {4,{790,1042},{110,110}}, {5,{655,681},{180,190}}, {6,{784,524},{110,110}},
  {7,{791,784},{110,110}}, {8,{375,859},{110,110}}, {9,{382,691},{110,110}},
  {10,{201,525},{110,110}}, {11,{221,649},{110,110}}, {12,{146,775},{110,110}},
  {13,{327,1023},{110,110}}
 };
 const TCHAR* Names[] = {TEXT("자수정"),TEXT("입구"),TEXT("삼엽충"),TEXT("고사리잎"),TEXT("아르켈론"),
  TEXT("알로사우루스"),TEXT("화석"),TEXT("물고기화석"),TEXT("진주화석"),TEXT("포유류 화석"),
  TEXT("거북이 화석"),TEXT("출구"),TEXT("탄생석")};
 FLinearColor Cyan() { return FLinearColor(0.08f,0.72f,1.f); }
}

void UNavFullMapWidget::NativeConstruct()
{
 Super::NativeConstruct(); bClosing=false; BuildSkinLayout();
}

bool UNavFullMapWidget::UsesReferenceArtwork() const
{
 return ReferenceTexture != nullptr && ReferenceCanvas != nullptr;
}

void UNavFullMapWidget::BuildSkinLayout()
{
 if (!WidgetTree || !Backdrop || !MapView || ReferenceCanvas) { return; }
 ReferenceTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/Nav/Reference/T_MuseumSimple.T_MuseumSimple"));
 MapView->RemoveFromParent(); MapView->Mode=ENavMinimapMode::Full;
 MapView->SetVisibility(ESlateVisibility::Collapsed);
 Backdrop->SetPadding(FMargin(0)); Backdrop->SetBrushColor(FLinearColor::Transparent);
 UCanvasPanel* ScreenCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); Backdrop->SetContent(ScreenCanvas);
 UBackgroundBlur* CameraBlur=WidgetTree->ConstructWidget<UBackgroundBlur>();
 CameraBlur->SetBlurStrength(8.f); CameraBlur->SetApplyAlphaToBlur(false);
 CameraBlur->SetVisibility(ESlateVisibility::HitTestInvisible);
 UCanvasPanelSlot* BlurSlot=ScreenCanvas->AddChildToCanvas(CameraBlur);
 BlurSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f)); BlurSlot->SetOffsets(FMargin(0));
 UBorder* CameraTint=WidgetTree->ConstructWidget<UBorder>();
 CameraTint->SetBrushColor(FLinearColor(0.003f,0.01f,0.02f,0.28f));
 CameraTint->SetVisibility(ESlateVisibility::HitTestInvisible);
 UCanvasPanelSlot* TintSlot=ScreenCanvas->AddChildToCanvas(CameraTint);
 TintSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f)); TintSlot->SetOffsets(FMargin(0));
 TintSlot->SetZOrder(1);
 UScaleBox* Scale=WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit);
 UCanvasPanelSlot* ScaleSlot=ScreenCanvas->AddChildToCanvas(Scale);
 ScaleSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f)); ScaleSlot->SetOffsets(FMargin(0));
 ScaleSlot->SetZOrder(2);
 USizeBox* Design=WidgetTree->ConstructWidget<USizeBox>(); Design->SetWidthOverride(ReferenceWidth);
 Design->SetHeightOverride(1495.f); Scale->SetContent(Design);
 ReferenceCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); Design->SetContent(ReferenceCanvas);
 auto Place=[](UCanvasPanel* Canvas,UWidget* Widget,float X,float Y,float Width,float Height)
 {
  UCanvasPanelSlot* CanvasSlot=Canvas->AddChildToCanvas(Widget);
  CanvasSlot->SetPosition(FVector2D(X,Y)); CanvasSlot->SetSize(FVector2D(Width,Height));
  CanvasSlot->SetZOrder(Canvas->GetChildrenCount());
 };
 // Keep the same bound map instance alive for existing asynchronous lifecycle setters.
 Place(ReferenceCanvas,MapView,0,0,1,1);
 auto Slice=[&](UCanvasPanel* Canvas,float SourceX,float SourceY,float Width,float Height,float X,float Y)
 {
  UImage* Image=WidgetTree->ConstructWidget<UImage>();
  if (ReferenceTexture)
  {
   FSlateBrush Brush; Brush.SetResourceObject(ReferenceTexture); Brush.DrawAs=ESlateBrushDrawType::Image;
   Brush.ImageSize=FVector2D(Width,Height);
   Brush.SetUVRegion(FBox2f(FVector2f(SourceX/ReferenceWidth,SourceY/ReferenceHeight),
    FVector2f((SourceX+Width)/ReferenceWidth,(SourceY+Height)/ReferenceHeight)));
   Image->SetBrush(Brush);
  }
  else { FSlateBrush EmptyBrush; EmptyBrush.DrawAs=ESlateBrushDrawType::NoDrawType; Image->SetBrush(EmptyBrush); }
  Image->SetVisibility(ESlateVisibility::HitTestInvisible); Place(Canvas,Image,X,Y,Width,Height); return Image;
 };
 // Restrained foreground artwork over the live camera, with independently scrollable cards.
 // Keep the approved simple panels and map; swap only Lexi's illustration.
 UBorder* TitleCap=WidgetTree->ConstructWidget<UBorder>();
 TitleCap->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.012f,0.023f,0.038f,1.f),40.f));
 TitleCap->SetBrushColor(FLinearColor::White); TitleCap->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(ReferenceCanvas,TitleCap,235,271,475,85);
 Slice(ReferenceCanvas,0,284,ReferenceWidth,941,0,284);
 // Center the equal-height Lexi/message row between Back and the map title.
 UScaleBox* HeaderScale=WidgetTree->ConstructWidget<UScaleBox>();
 HeaderScale->SetStretch(EStretch::ScaleToFit);
 UCanvasPanelSlot* HeaderSlot=ScreenCanvas->AddChildToCanvas(HeaderScale);
 HeaderLayoutSlot=HeaderSlot;
 HeaderSlot->SetAnchors(FAnchors(0.f,0.f,1.f,0.f));
 HeaderSlot->SetOffsets(FMargin(16.f,144.f,16.f,200.f)); HeaderSlot->SetZOrder(3);
 USizeBox* HeaderSize=WidgetTree->ConstructWidget<USizeBox>();
 HeaderDesignSize=HeaderSize;
 HeaderSize->SetWidthOverride(1203.f); HeaderSize->SetHeightOverride(265.f);
 HeaderScale->SetContent(HeaderSize);
 UCanvasPanel* HeaderCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); HeaderSize->SetContent(HeaderCanvas);
 UTexture2D* LexiTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/Nav/Reference/T_MuseumForeground.T_MuseumForeground"));
 auto LexiSection=[&](float Y,float Width,float Height)
 {
  UImage* Lexi=WidgetTree->ConstructWidget<UImage>();
  FSlateBrush Brush; Brush.SetResourceObject(LexiTexture); Brush.DrawAs=ESlateBrushDrawType::Image;
  Brush.ImageSize=FVector2D(Width,Height);
  Brush.SetUVRegion(FBox2f(FVector2f(130.f/941.f,Y/1671.f),FVector2f((130.f+Width)/941.f,(Y+Height)/1671.f)));
  Lexi->SetBrush(Brush); Lexi->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(HeaderCanvas,Lexi,0,Y,Width,Height);
 };
 if(LexiTexture) { LexiSection(0,260,195); LexiSection(195,245,70); }
 UBorder* MessagePanel=WidgetTree->ConstructWidget<UBorder>();
 const FLinearColor BubbleColor(0.012f,0.023f,0.038f,1.f);
 MessagePanel->SetBrush(FSlateRoundedBoxBrush(BubbleColor,28.f));
 MessagePanel->SetBrushColor(FLinearColor::White);
 MessagePanel->SetVisibility(ESlateVisibility::HitTestInvisible);
 UBorder* BubbleTail=WidgetTree->ConstructWidget<UBorder>();
 BubbleTail->SetBrushColor(BubbleColor); BubbleTail->SetRenderTransformAngle(45.f);
 BubbleTail->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(HeaderCanvas,BubbleTail,268,120,28,28);
 Place(HeaderCanvas,MessagePanel,280,0,923,265);
 MessageLayoutSlot=Cast<UCanvasPanelSlot>(MessagePanel->Slot);
 UFont* Face=LoadObject<UFont>(nullptr,TEXT("/Game/UI/Fonts/Pretendard-SemiBold_Font.Pretendard-SemiBold_Font"));
 auto Label=[&](UCanvasPanel* Canvas,const FString& Value,int32 Size,float X,float Y,float Width,float Height)
 {
  UTextBlock* Text=WidgetTree->ConstructWidget<UTextBlock>(); Text->SetText(FText::FromString(Value));
  FSlateFontInfo Font=Text->GetFont(); Font.Size=Size; if(Face) {Font.FontObject=Face;} Text->SetFont(Font);
  Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.7f,0.9f,1.f))); Text->SetJustification(ETextJustify::Center);
  Text->SetVisibility(ESlateVisibility::HitTestInvisible); Place(Canvas,Text,X,Y,Width,Height); return Text;
 };
 UTextBlock* MessageTitle=Label(HeaderCanvas,TEXT("도착지를 골라주세요!"),40,312,73,859,64);
 MessageTitle->SetColorAndOpacity(FSlateColor(FLinearColor::White));
 MessageTitleSlot=Cast<UCanvasPanelSlot>(MessageTitle->Slot);
 UTextBlock* MessageSubtitle=Label(HeaderCanvas,TEXT("원하는 전시물을 선택하세요."),27,312,151,859,48);
 MessageSubtitleSlot=Cast<UCanvasPanelSlot>(MessageSubtitle->Slot);
 FButtonStyle TransparentStyle;
 FSlateBrush Empty; Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
 TransparentStyle.SetNormal(Empty); TransparentStyle.SetHovered(Empty); TransparentStyle.SetPressed(Empty);
 TransparentStyle.SetDisabled(Empty); TransparentStyle.SetNormalPadding(FMargin(0)); TransparentStyle.SetPressedPadding(FMargin(0));
 auto HotButton=[&](UCanvasPanel* Canvas,int32 Number,float X,float Y,float Width,float Height)
 {
  UNavDestButton* Button=WidgetTree->ConstructWidget<UNavDestButton>(); Button->SetStyle(TransparentStyle);
  Button->SetTouchMethod(EButtonTouchMethod::PreciseTap); Button->SetClickMethod(EButtonClickMethod::PreciseClick);
  Button->SetIsEnabled(false); Button->WireClick(); Button->OnDestClicked.AddDynamic(this,&UNavFullMapWidget::HandleDestButtonClicked);
  Button->SetToolTipText(FText::FromString(Names[Number-1])); Place(Canvas,Button,X,Y,Width,Height);
  DestButtons.Add(Button); DestinationButtonNumbers.Add(Number); return Button;
 };
 for (const FReferenceHotspot& Hotspot:Hotspots)
 {
  HotButton(ReferenceCanvas,Hotspot.Number,Hotspot.Center.X-Hotspot.Size.X*0.5f,
   Hotspot.Center.Y-Hotspot.Size.Y*0.5f-TopCrop,Hotspot.Size.X,Hotspot.Size.Y);
 }
 UScrollBox* Carousel=WidgetTree->ConstructWidget<UScrollBox>(); Carousel->SetOrientation(Orient_Horizontal);
 Carousel->SetScrollBarVisibility(ESlateVisibility::Collapsed); Carousel->SetClipping(EWidgetClipping::ClipToBounds);
 Place(ReferenceCanvas,Carousel,0,1225-TopCrop,ReferenceWidth,270);
 USizeBox* StripSize=WidgetTree->ConstructWidget<USizeBox>(); StripSize->SetWidthOverride(ReferenceWidth+10*286.f);
 StripSize->SetHeightOverride(270); Carousel->AddChild(StripSize);
 UCanvasPanel* Strip=WidgetTree->ConstructWidget<UCanvasPanel>(); StripSize->SetContent(Strip);
 Slice(Strip,0,1225,ReferenceWidth,270,0,0);
 HotButton(Strip,1,55,23,274,239); HotButton(Strip,3,336,7,273,256); HotButton(Strip,5,615,23,272,239);
 // The initial viewport is exactly the supplied strip. Swipe reveals the other live destinations.
 int32 ExtraIndex=0;
 for (const FReferenceHotspot& Hotspot:Hotspots)
 {
  if(Hotspot.Number==1 || Hotspot.Number==3 || Hotspot.Number==5) {continue;}
  const float X=ReferenceWidth+ExtraIndex++*286.f;
  UBorder* Card=WidgetTree->ConstructWidget<UBorder>();
  const FString Name=Names[Hotspot.Number-1];
  const FLinearColor Accent(0.16f,0.28f,0.4f,1.f);
  Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor(0.012f,0.025f,0.045f,0.94f),22.f,Accent,1.f));
  Card->SetBrushColor(FLinearColor::White); Card->SetVisibility(ESlateVisibility::HitTestInvisible); Place(Strip,Card,X+8,20,270,244);
  Slice(Strip,Hotspot.Center.X-50,Hotspot.Center.Y-50,100,100,X+93,36);
  Label(Strip,FString::Printf(TEXT("%d  %s"),Hotspot.Number,*Name),22,X+15,148,255,42);
  Label(Strip,TEXT("길 안내 시작  ›"),17,X+24,209,237,36);
  HotButton(Strip,Hotspot.Number,X+8,20,270,244);
 }
 // Anchor close to the screen's top-left, independent of the centered artwork.
 UButton* Back=WidgetTree->ConstructWidget<UButton>(); FButtonStyle BackStyle=TransparentStyle;
 BackStyle.SetNormal(FSlateRoundedBoxBrush(FLinearColor(0.012f,0.016f,0.023f,0.9f),56.f));
 BackStyle.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.035f,0.06f,0.09f,0.95f),56.f));
 BackStyle.SetPressed(BackStyle.Hovered); Back->SetStyle(BackStyle);
 UTextBlock* BackText=WidgetTree->ConstructWidget<UTextBlock>(); BackText->SetText(FText::FromString(TEXT("‹")));
 FSlateFontInfo BackFont=BackText->GetFont(); BackFont.Size=54; BackText->SetFont(BackFont);
 BackText->SetJustification(ETextJustify::Center); BackText->SetColorAndOpacity(FSlateColor(FLinearColor::White)); Back->SetContent(BackText);
 Back->SetToolTipText(FText::FromString(TEXT("지도 닫기"))); Back->OnClicked.AddDynamic(this,&UNavFullMapWidget::Close);
 Place(ScreenCanvas,Back,16,16,112,112);
 MapStatus=Label(ReferenceCanvas,ReferenceTexture ? TEXT("지도 연결 중…") : TEXT("안내도 이미지를 불러올 수 없습니다"),23,190,1140-TopCrop,570,46);
}

void UNavFullMapWidget::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
 Super::NativeTick(MyGeometry,InDeltaTime);
 if(HeaderLayoutSlot && ReferenceCanvas && MyGeometry.GetLocalSize().GetMin()>0.f)
 {
  const FVector2D ViewSize=MyGeometry.GetLocalSize();
  const float ArtScale=FMath::Min(ViewSize.X/ReferenceWidth,ViewSize.Y/1495.f);
  const float MapTitleTop=(ViewSize.Y-1495.f*ArtScale)*0.5f+271.f*ArtScale;
  const float Gap=FMath::Max(0.f,MapTitleTop-128.f);
  const float HeaderHeight=FMath::Min(220.f,FMath::Max(0.f,Gap-32.f));
  const float HeaderTop=128.f+(Gap-HeaderHeight)*0.5f;
  const FMargin Desired(16.f,HeaderTop,16.f,HeaderHeight);
  if(HeaderLayoutSlot->GetOffsets()!=Desired) {HeaderLayoutSlot->SetOffsets(Desired);}
  if(HeaderHeight>0.f && HeaderDesignSize && MessageLayoutSlot && MessageTitleSlot && MessageSubtitleSlot)
  {
   // Use the entire available width without stretching Lexi or the lettering.
   const float DesignWidth=FMath::Max(690.f,(ViewSize.X-32.f)*265.f/HeaderHeight);
   if(!FMath::IsNearlyEqual(HeaderDesignSize->GetWidthOverride(),DesignWidth))
   {
    HeaderDesignSize->SetWidthOverride(DesignWidth);
    MessageLayoutSlot->SetSize(FVector2D(DesignWidth-280.f,265.f));
    MessageTitleSlot->SetSize(FVector2D(DesignWidth-344.f,64.f));
    MessageSubtitleSlot->SetSize(FVector2D(DesignWidth-344.f,48.f));
   }
  }
 }
 if(!MapView || !MapStatus || MapView->HasGraph()) {MapWaitSeconds=0.f; return;}
 MapWaitSeconds+=InDeltaTime;
 if(MapWaitSeconds>=10.f && ReferenceTexture) {MapStatus->SetText(FText::FromString(TEXT("지도 연결을 확인해주세요")));}
}

void UNavFullMapWidget::ApplyState(const FNavGraph& InGraph,const TArray<FVector2D>& InRouteXY,
 bool bHasPose,const FVector2D& PoseXY,float PoseHeadingDeg,bool bPoseHasHeading,const FString& InDestNodeId)
{
 BuildSkinLayout(); if(!MapView) {return;}
 MapView->Mode=ENavMinimapMode::Full; MapView->SetGraph(InGraph); MapView->SetRouteXY(InRouteXY);
 MapView->SetDestinationNode(InDestNodeId);
 if(bHasPose) {MapView->SetCurrentPose(PoseXY.X,PoseXY.Y,PoseHeadingDeg,bPoseHasHeading);} else {MapView->ClearCurrentPose();}
 RefreshDestinations(InGraph,InDestNodeId);
}

void UNavFullMapWidget::Close()
{
 if(bClosing) {return;} bClosing=true; OnClosed.Broadcast(); RemoveFromParent();
}

bool UNavFullMapWidget::FindReferenceDestinationAtLocal(const FVector2D& SourceImagePosition,FString& OutNodeId) const
{
 OutNodeId.Reset(); if(!UsesReferenceArtwork() || bClosing) {return false;}
 for(const FReferenceHotspot& Hotspot:Hotspots)
 {
  const FVector2D Offset=SourceImagePosition-Hotspot.Center;
  if(FMath::Abs(Offset.X)<=Hotspot.Size.X*0.5f && FMath::Abs(Offset.Y)<=Hotspot.Size.Y*0.5f)
  {
   const FString* NodeId=ReferenceNodeIds.Find(Hotspot.Number);
   if(NodeId && !NodeId->IsEmpty()) {OutNodeId=*NodeId; return true;}
  }
 }
 return false;
}

FReply UNavFullMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry,const FPointerEvent& InMouseEvent)
{
 // Transparent map/card buttons own selection. Empty background taps stay open.
 return FReply::Handled();
}

void UNavFullMapWidget::HandleDestButtonClicked(const FString& NodeId)
{
 if(bClosing || !UsesReferenceArtwork() || NodeId.IsEmpty()) {return;}
 bool bLiveNode=false; for(const TPair<int32,FString>& Entry:ReferenceNodeIds) {if(Entry.Value==NodeId) {bLiveNode=true; break;}}
 if(!bLiveNode) {return;} if(MapView) {MapView->SetDestinationNode(NodeId);}
 OnDestinationChosen.Broadcast(NodeId); Close();
}

void UNavFullMapWidget::RefreshDestinations(const FNavGraph& InGraph,const FString& InDestNodeId)
{
 BuildSkinLayout(); ActiveDestination=InDestNodeId; ReferenceNodeIds.Reset();
 for(const FNavMapNode& Node:InGraph.Nodes)
 {
  const int32 Number=FNavDestinations::DisplayNumber(Node.Label);
  if(Number!=0 && FNavDestinations::IsDestination(Node.NodeType) && !Node.NodeId.IsEmpty())
  {
   // Deterministic if malformed data provides duplicate display labels.
   FString* Existing=ReferenceNodeIds.Find(Number); if(!Existing || Node.NodeId<*Existing) {ReferenceNodeIds.Add(Number,Node.NodeId);}
  }
 }
 BuildDestinationButtons(InGraph);
}

void UNavFullMapWidget::BuildDestinationButtons(const FNavGraph& InGraph)
{
 for(int32 Index=0; Index<DestButtons.Num(); ++Index)
 {
  UNavDestButton* Button=DestButtons[Index]; if(!Button) {continue;}
  const FString* NodeId=ReferenceNodeIds.Find(DestinationButtonNumbers[Index]);
  Button->NodeId=NodeId ? *NodeId : FString(); Button->SetIsEnabled(UsesReferenceArtwork() && NodeId && !bClosing);
 }
 if(MapStatus)
 {
  MapStatus->SetVisibility(UsesReferenceArtwork() && ReferenceNodeIds.Num()>0 ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
  if(UsesReferenceArtwork() && InGraph.Nodes.Num()>0 && ReferenceNodeIds.Num()==0)
   {MapStatus->SetText(FText::FromString(TEXT("이 안내도의 목적지 정보를 확인해주세요")));}
 }
}
