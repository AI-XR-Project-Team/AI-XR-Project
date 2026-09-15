#include "NavFullMapWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/BackgroundBlur.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "NavDestButton.h"
#include "NavDestinations.h"

namespace
{
 constexpr float SourceWidth=429.f, SourceHeight=555.f, MapCropHeight=532.f;
 constexpr float DesignWidth=940.f, DesignHeight=1740.f;
 constexpr float MapX=20.f, MapY=280.f, MapWidth=900.f, MapScale=MapWidth/SourceWidth;
 struct FReferenceHotspot {int32 Number; FVector2D Center; FVector2D Size; const TCHAR* Gallery;};
 // Stable backend label keys, not the numbers printed in the new artwork.
 // Artwork prints entrance as 1 and amethyst as 2; resolve their unchanged backend labels.
 const FReferenceHotspot Hotspots[]={
  {1,{50,486},{40,44},TEXT("mineral")},{2,{50,308},{36,68},TEXT("all")},
  {3,{292,393},{42,42},TEXT("paleo")},
  {4,{391,443},{40,42},TEXT("paleo")},{5,{319,161},{56,58},TEXT("marine")},
  {6,{375,99},{42,42},TEXT("marine")},{7,{378,283},{42,42},TEXT("marine")},
  {8,{155,319},{42,42},TEXT("paleo")},{9,{156,207},{42,42},TEXT("paleo")},
  {10,{70,90},{42,42},TEXT("ceno")},{11,{80,170},{42,42},TEXT("ceno")},
  {12,{51,243},{36,34},TEXT("all")},{13,{124,408},{42,42},TEXT("mineral")}};
 const TCHAR* Names[]={TEXT("자수정"),TEXT("입구"),TEXT("삼엽충"),TEXT("고사리잎"),TEXT("아르켈론"),
  TEXT("알로사우루스"),TEXT("화석"),TEXT("물고기화석"),TEXT("진주화석"),TEXT("포유류 화석"),
  TEXT("거북이 화석"),TEXT("출구"),TEXT("탄생석")};
 const TCHAR* GalleryIds[]={TEXT("all"),TEXT("ceno"),TEXT("paleo"),TEXT("marine"),TEXT("mineral")};
 const TCHAR* GalleryNames[]={TEXT("전체"),TEXT("신생대관"),TEXT("고생대관"),TEXT("해양생물관"),TEXT("암석관")};
 const TCHAR* SpecimenDescriptions[]={
  TEXT("보랏빛 광물의 모양과 색을 살펴보세요."),
  TEXT("렉시와 함께 박물관 탐험을 시작해볼까요?"),
  TEXT("오래전 바다 생물의 흔적을 만나보세요."),
  TEXT("잎에 남은 무늬에서 옛 식물의 모습을 찾아보세요."),
  TEXT("커다란 바다거북의 모습을 살펴보세요."),
  TEXT("날카로운 이빨과 두개골을 살펴보세요."),
  TEXT("돌에 남은 옛 생물의 흔적을 찾아보세요."),
  TEXT("지느러미와 몸의 흔적을 찾아보세요."),
  TEXT("화석의 모양과 표면을 가까이 살펴보세요."),
  TEXT("옛 동물의 모습을 상상해보세요."),
  TEXT("화석에 남은 거북의 몸과 등껍질을 살펴보세요."),
  TEXT("이곳에서 관람을 마칠 수 있어요."),
  TEXT("다양한 보석과 광물의 색을 만나보세요.")};
 const TCHAR* GalleryDescriptions[]={TEXT("화석을 고르면 렉시가 안내해 드려요"),
  TEXT("포유류와 거북이 화석을 만나볼 수 있어요."),
  TEXT("삼엽충과 식물·물고기 화석이 있어요."),
  TEXT("아르켈론과 공룡 화석을 만나보세요."),
  TEXT("자수정과 탄생석의 색과 모양을 살펴보세요.")};
 FLinearColor Accent(){return FLinearColor(0.15f,0.78f,0.91f,1.f);}
 FLinearColor Panel(){return FLinearColor(0.013f,0.027f,0.044f,0.97f);}
 FButtonStyle ButtonStyle(bool Selected=false)
 {
  FButtonStyle S;
  S.SetNormal(FSlateRoundedBoxBrush(Selected?FLinearColor(0.03f,0.18f,0.23f,1.f):Panel(),18.f,
   Selected?Accent():FLinearColor(0.10f,0.17f,0.22f,1.f),Selected?2.f:1.f));
  S.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.04f,0.22f,0.27f,1.f),18.f,Accent(),2.f));
  S.SetPressed(S.Hovered); S.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(0.045f,0.067f,0.083f,1.f),18.f));
  S.SetNormalPadding(FMargin(0)); S.SetPressedPadding(FMargin(0)); return S;
 }
 TArray<TArray<FVector2D>> GalleryPolygons(const FString& Id)
 {
  // Illustration-space boundaries only; museum_final metric routing is independent.
  if(Id==TEXT("ceno")) return {{{42,34},{58,34},{58,48},{79,48},{79,34},{98,34},{98,74},
   {108,74},{108,255},{33,255},{33,234},{42,234}}};
  if(Id==TEXT("marine")) return {{{346,40},{414,40},{414,179},{418,185},{418,218},{407,223},
   {407,316},{307,316},{307,302},{283,275},{277,252},{277,112},{273,105},{343,105}}};
  if(Id==TEXT("paleo")) return {
   {{118,151},{125,151},{125,166},{199,166},{199,151},{238,151},{238,225},{213,226},
    {193,248},{193,291},{200,291},{200,343},{181,343},{181,352},{129,352},{129,257},{118,257}},
   {{308,337},{405,337},{405,461},{398,474},{369,474},{364,482},{325,482},{320,466},
    {273,466},{273,381},{281,371},{297,368},{297,353},{308,353}}};
  if(Id==TEXT("mineral")) return {{{42,357},{61,357},{66,370},{148,370},{148,441},{77,441},
   {77,427},{66,427},{66,458},{170,458},{170,450},{200,450},{200,462},{187,462},
   {187,474},{146,474},{146,482},{113,482},{113,475},{104,475},{104,485},
   {92,485},{92,502},{67,502},{67,515},{29,515},{29,472},{42,472}}};
  return {};
 }
}

void UNavFullMapWidget::NativeConstruct(){Super::NativeConstruct(); bClosing=false; BuildSkinLayout();}
bool UNavFullMapWidget::UsesReferenceArtwork() const{return ReferenceTexture && ReferenceCanvas;}

void UNavFullMapWidget::BuildSkinLayout()
{
 if(!WidgetTree || !Backdrop || !MapView || ReferenceCanvas) return;
 ReferenceTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/Nav/Reference/T_MuseumFloorplan.T_MuseumFloorplan"));
 MapView->RemoveFromParent(); MapView->Mode=ENavMinimapMode::Full; MapView->SetVisibility(ESlateVisibility::Collapsed);
 Backdrop->SetPadding(FMargin(0)); Backdrop->SetBrushColor(FLinearColor::Transparent);
 UCanvasPanel* Screen=WidgetTree->ConstructWidget<UCanvasPanel>(); Backdrop->SetContent(Screen);
 auto Place=[](UCanvasPanel* Canvas,UWidget* Widget,float X,float Y,float W,float H)
 {
  UCanvasPanelSlot* Slot=Canvas->AddChildToCanvas(Widget); Slot->SetPosition(FVector2D(X,Y)); Slot->SetSize(FVector2D(W,H));
  Slot->SetZOrder(Canvas->GetChildrenCount()); return Slot;
 };
 UBackgroundBlur* Blur=WidgetTree->ConstructWidget<UBackgroundBlur>();
 Blur->SetBlurStrength(8.f); Blur->SetApplyAlphaToBlur(false); Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
 UCanvasPanelSlot* Full=Place(Screen,Blur,0,0,0,0); Full->SetAnchors(FAnchors(0,0,1,1)); Full->SetOffsets(FMargin(0));
 UBorder* Tint=WidgetTree->ConstructWidget<UBorder>(); Tint->SetBrushColor(FLinearColor(0.003f,0.01f,0.02f,0.48f));
 Tint->SetVisibility(ESlateVisibility::HitTestInvisible);
 Full=Place(Screen,Tint,0,0,0,0); Full->SetAnchors(FAnchors(0,0,1,1)); Full->SetOffsets(FMargin(0));
 UScaleBox* Scale=WidgetTree->ConstructWidget<UScaleBox>(); Scale->SetStretch(EStretch::ScaleToFit);
 Full=Place(Screen,Scale,0,0,0,0); Full->SetAnchors(FAnchors(0,0,1,1)); Full->SetOffsets(FMargin(0));
 USizeBox* Design=WidgetTree->ConstructWidget<USizeBox>();
 Design->SetWidthOverride(DesignWidth); Design->SetHeightOverride(DesignHeight); Scale->SetContent(Design);
 ReferenceCanvas=WidgetTree->ConstructWidget<UCanvasPanel>(); Design->SetContent(ReferenceCanvas);
 Place(ReferenceCanvas,MapView,0,0,1,1);
 UFont* Face=LoadObject<UFont>(nullptr,TEXT("/Game/UI/Fonts/Pretendard-SemiBold_Font.Pretendard-SemiBold_Font"));
 auto Text=[&](const TCHAR* Value,int32 Size,FLinearColor Color)
 {
  UTextBlock* T=WidgetTree->ConstructWidget<UTextBlock>(); T->SetText(FText::FromString(Value));
  FSlateFontInfo Font=T->GetFont(); Font.Size=Size; if(Face) Font.FontObject=Face; T->SetFont(Font);
  T->SetColorAndOpacity(FSlateColor(Color)); T->SetVisibility(ESlateVisibility::HitTestInvisible); return T;
 };
 auto Label=[&](const TCHAR* Value,int32 Size,float X,float Y,float W,float H,FLinearColor Color=FLinearColor::White)
 {
  UTextBlock* T=Text(Value,Size,Color); Place(ReferenceCanvas,T,X,Y,W,H); return T;
 };
 auto Image=[&](UTexture2D* Texture,FVector2f UV0,FVector2f UV1,float X,float Y,float W,float H)
 {
  UImage* I=WidgetTree->ConstructWidget<UImage>(); FSlateBrush B;
  B.SetResourceObject(Texture); B.DrawAs=Texture?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
  B.ImageSize=FVector2D(W,H); B.SetUVRegion(FBox2f(UV0,UV1)); I->SetBrush(B);
  I->SetVisibility(ESlateVisibility::HitTestInvisible); Place(ReferenceCanvas,I,X,Y,W,H);
 };
 UButton* Back=WidgetTree->ConstructWidget<UButton>(); Back->SetStyle(ButtonStyle());
 UTextBlock* BackText=Text(TEXT("‹"),60,FLinearColor::White); BackText->SetJustification(ETextJustify::Center);
 Back->SetContent(BackText); Back->SetToolTipText(FText::FromString(TEXT("지도 닫기")));
 Back->OnClicked.AddDynamic(this,&UNavFullMapWidget::Close);
 BackLayoutSlot=Place(Screen,Back,16,16,96,96); BackLabel=BackText;
 UTextBlock* MuseumTitle=Label(TEXT("경주 자연사박물관"),28,150,38,680,48); MuseumTitle->SetJustification(ETextJustify::Center);
 UTexture2D* Lexi=LoadObject<UTexture2D>(nullptr,TEXT("/Game/UI/Nav/Reference/T_MuseumForeground.T_MuseumForeground"));
 Image(Lexi,{130.f/941.f,0},{390.f/941.f,195.f/1671.f},22,108,176,132);
 Image(Lexi,{130.f/941.f,195.f/1671.f},{375.f/941.f,265.f/1671.f},22,240,166,47);
 UBorder* Bubble=WidgetTree->ConstructWidget<UBorder>(); Bubble->SetBrush(FSlateRoundedBoxBrush(Panel(),24.f));
 Bubble->SetBrushColor(FLinearColor::White); Bubble->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(ReferenceCanvas,Bubble,220,118,696,144);
 UBorder* Tail=WidgetTree->ConstructWidget<UBorder>(); Tail->SetBrushColor(Panel()); Tail->SetRenderTransformAngle(45.f);
 Tail->SetVisibility(ESlateVisibility::HitTestInvisible); Place(ReferenceCanvas,Tail,210,176,22,22);
 LexiTitle=Label(TEXT("어디로 가볼까요?"),32,252,138,638,52);
 LexiDescription=Label(TEXT("화석을 고르면 렉시가 안내해 드려요"),22,252,204,638,40,FLinearColor(0.7f,0.82f,0.9f));
 // UMG crops only the incomplete footer; the supplied PNG is preserved byte for byte.
 Image(ReferenceTexture,{0,0},{1,MapCropHeight/SourceHeight},MapX,MapY,MapWidth,MapCropHeight*MapScale);
 for(const FReferenceHotspot& H:Hotspots)
 {
  UNavDestButton* B=WidgetTree->ConstructWidget<UNavDestButton>(); B->SetIsEnabled(false);
  B->SetTouchMethod(EButtonTouchMethod::DownAndUp); B->WireClick();
  B->OnDestClicked.AddDynamic(this,&UNavFullMapWidget::HandleDestButtonClicked);
  B->SetToolTipText(FText::FromString(Names[H.Number-1]));
  Place(ReferenceCanvas,B,MapX+(H.Center.X-H.Size.X/2)*MapScale,MapY+(H.Center.Y-H.Size.Y/2)*MapScale,H.Size.X*MapScale,H.Size.Y*MapScale);
  DestButtons.Add(B); DestinationButtonNumbers.Add(H.Number);
 }
 Label(TEXT("전시관 둘러보기"),23,28,1404,880,38,FLinearColor(0.65f,0.77f,0.83f));
 const float Widths[]={108,182,182,220,164}; float X=20;
 for(int32 I=0;I<5;++I)
 {
  UNavDestButton* B=WidgetTree->ConstructWidget<UNavDestButton>(); B->NodeId=GalleryIds[I]; B->WireClick();
  B->OnDestClicked.AddDynamic(this,&UNavFullMapWidget::HandleGalleryClicked);
  UTextBlock* T=Text(GalleryNames[I],26,FLinearColor::White); T->SetJustification(ETextJustify::Center);
  B->SetContent(T); Place(ReferenceCanvas,B,X,1448,Widths[I],76); X+=Widths[I]+11;
  GalleryButtons.Add(B); GalleryLabels.Add(T);
 }
 UBorder* Selection=WidgetTree->ConstructWidget<UBorder>();
 Selection->SetBrush(FSlateRoundedBoxBrush(Panel(),24.f,FLinearColor(0.09f,0.18f,0.23f),1.f));
 Selection->SetBrushColor(FLinearColor::White); Selection->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(ReferenceCanvas,Selection,20,1542,900,150);
 SelectionTitle=Label(TEXT("전시물을 선택해주세요"),32,48,1560,530,50);
 SelectionSubtitle=Label(TEXT("지도 위 화석을 눌러보세요"),22,48,1623,530,38,FLinearColor(0.6f,0.72f,0.8f));
 GuideButton=WidgetTree->ConstructWidget<UButton>(); GuideButton->SetStyle(ButtonStyle(true));
 UTextBlock* GuideText=Text(TEXT("여기로 안내  ›"),28,FLinearColor::White); GuideText->SetJustification(ETextJustify::Center);
 GuideButton->SetContent(GuideText); GuideButton->OnClicked.AddDynamic(this,&UNavFullMapWidget::StartSelectedGuidance);
 Place(ReferenceCanvas,GuideButton,604,1564,290,106);
 MapStatus=Label(TEXT("안내 정보 연결 중…"),21,30,1700,880,34,FLinearColor(0.65f,0.77f,0.83f));
 MapStatus->SetJustification(ETextJustify::Center); UpdateSelectionAppearance();
}

void UNavFullMapWidget::NativeTick(const FGeometry& Geometry,float Delta)
{
 Super::NativeTick(Geometry,Delta);
 if(BackLayoutSlot && BackLabel && Geometry.GetLocalSize().X>0)
 {
  const FVector2D View=Geometry.GetLocalSize();
  const float Scale=FMath::Min(View.X/DesignWidth,View.Y/DesignHeight);
  const float Size=FMath::Clamp(96.f*Scale,48.f,96.f);
  const float Margin=FMath::Clamp(16.f*Scale,8.f,16.f);
  BackLayoutSlot->SetPosition(FVector2D(Margin,Margin)); BackLayoutSlot->SetSize(FVector2D(Size,Size));
  FSlateFontInfo Font=BackLabel->GetFont(); const int32 FontSize=FMath::RoundToInt(Size*.625f);
  if(Font.Size!=FontSize){Font.Size=FontSize;BackLabel->SetFont(Font);}
 }
 if(ReferenceNodeIds.Num()>0 || !MapStatus){MapWaitSeconds=0;return;}
 MapWaitSeconds+=Delta;
 if(!ReferenceTexture) MapStatus->SetText(FText::FromString(TEXT("지도 이미지를 불러올 수 없습니다")));
 else if(MapWaitSeconds>=10.f) MapStatus->SetText(FText::FromString(TEXT("안내 정보 연결을 확인해주세요")));
}
void UNavFullMapWidget::ApplyState(const FNavGraph& Graph,const TArray<FVector2D>& Route,bool HasPose,const FVector2D& Pose,float Heading,bool HasHeading,const FString& Destination)
{
 BuildSkinLayout(); if(!MapView)return;
 MapView->Mode=ENavMinimapMode::Full; MapView->SetGraph(Graph); MapView->SetRouteXY(Route); MapView->SetDestinationNode(Destination);
 if(HasPose) MapView->SetCurrentPose(Pose.X,Pose.Y,Heading,HasHeading); else MapView->ClearCurrentPose();
 RefreshDestinations(Graph,Destination);
}
void UNavFullMapWidget::Close(){if(bClosing)return; bClosing=true; OnClosed.Broadcast(); RemoveFromParent();}
bool UNavFullMapWidget::FindReferenceDestinationAtLocal(const FVector2D& P,FString& Out) const
{
 Out.Reset(); if(!UsesReferenceArtwork() || bClosing)return false;
 for(const FReferenceHotspot& H:Hotspots)
 {
  const FVector2D D=P-H.Center;
  if(FMath::Abs(D.X)<=H.Size.X/2 && FMath::Abs(D.Y)<=H.Size.Y/2)
  {
   const FString* Id=ReferenceNodeIds.Find(H.Number);
   if(Id && !Id->IsEmpty()){Out=*Id;return true;}
  }
 }
 return false;
}
FReply UNavFullMapWidget::NativeOnMouseButtonDown(const FGeometry&,const FPointerEvent&){return FReply::Handled();}
void UNavFullMapWidget::HandleDestButtonClicked(const FString& Id){SelectDestination(Id);}
void UNavFullMapWidget::HandleGalleryClicked(const FString& Id){SelectGallery(Id);}
void UNavFullMapWidget::SelectDestination(const FString& Id)
{
 if(bClosing || !UsesReferenceArtwork() || Id.IsEmpty())return;
 for(const FReferenceHotspot& H:Hotspots)
 {
  const FString* Live=ReferenceNodeIds.Find(H.Number);
  if(Live && *Live==Id)
  {
   if(SelectedDestination==Id){SelectedDestination.Reset();SelectedGallery=TEXT("all");}
   else {SelectedDestination=Id;SelectedGallery=H.Gallery;}
   UpdateSelectionAppearance();return;
  }
 }
}
void UNavFullMapWidget::SelectGallery(const FString& Id)
{
 if(bClosing)return;
 bool Valid=false;for(const TCHAR* Gallery:GalleryIds)if(Id==Gallery)Valid=true;
 if(!Valid)return;
 SelectedGallery=SelectedGallery==Id?TEXT("all"):Id;
 SelectedDestination.Reset(); UpdateSelectionAppearance();
}
void UNavFullMapWidget::StartSelectedGuidance()
{
 if(bClosing || SelectedDestination.IsEmpty() || !UsesReferenceArtwork())return;
 bool Live=false;for(const auto& Entry:ReferenceNodeIds)if(Entry.Value==SelectedDestination)Live=true;
 if(!Live)return;
 const FString Id=SelectedDestination;
 // Latch before broadcasting to reject rapid taps and reentrant route handlers.
 bClosing=true; if(GuideButton)GuideButton->SetIsEnabled(false);
 if(MapView)MapView->SetDestinationNode(Id);
 OnDestinationChosen.Broadcast(Id); OnClosed.Broadcast(); RemoveFromParent();
}
void UNavFullMapWidget::RefreshDestinations(const FNavGraph& Graph,const FString& Destination)
{
 BuildSkinLayout(); ReferenceNodeIds.Reset();
 for(const FNavMapNode& N:Graph.Nodes)
 {
  const int32 Number=FNavDestinations::DisplayNumber(N.Label);
  if(Number && FNavDestinations::IsDestination(N.NodeType) && !N.NodeId.IsEmpty())
  {
   FString* Existing=ReferenceNodeIds.Find(Number);
   if(!Existing || N.NodeId<*Existing)ReferenceNodeIds.Add(Number,N.NodeId);
  }
 }
 bool Valid=false;for(const auto& Entry:ReferenceNodeIds)if(Entry.Value==SelectedDestination)Valid=true;
 if(!Valid)SelectedDestination.Reset();
 // Pose refreshes must not undo an unconfirmed selection or a gallery change.
 if(Destination!=ActiveDestination && SelectedDestination.IsEmpty())SelectDestination(Destination);
 ActiveDestination=Destination; BuildDestinationButtons(Graph); UpdateSelectionAppearance();
}
void UNavFullMapWidget::BuildDestinationButtons(const FNavGraph& Graph)
{
 for(int32 I=0;I<DestButtons.Num();++I)
 {
  const FString* Id=ReferenceNodeIds.Find(DestinationButtonNumbers[I]);
  DestButtons[I]->NodeId=Id?*Id:FString(); DestButtons[I]->SetIsEnabled(UsesReferenceArtwork() && Id && !bClosing);
 }
 if(MapStatus)
 {
  MapStatus->SetVisibility(UsesReferenceArtwork() && ReferenceNodeIds.Num()>0?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
  if(UsesReferenceArtwork() && Graph.Nodes.Num()>0 && ReferenceNodeIds.Num()==0)
   MapStatus->SetText(FText::FromString(TEXT("이 지도의 전시물 정보를 확인해주세요")));
 }
}
void UNavFullMapWidget::UpdateSelectionAppearance()
{
 int32 SelectedNumber=0;
 for(int32 I=0;I<DestButtons.Num();++I)
 {
  UNavDestButton* B=DestButtons[I];const bool Selected=!SelectedDestination.IsEmpty() && B->NodeId==SelectedDestination;
  if(Selected)SelectedNumber=DestinationButtonNumbers[I];
  FButtonStyle S;FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;
  S.SetNormal(Selected?FSlateRoundedBoxBrush(FLinearColor(0.05f,0.7f,0.85f,0.08f),Accent(),3.f):Empty);
  S.SetHovered(FSlateRoundedBoxBrush(FLinearColor(0.1f,0.8f,1.f,0.06f),Accent(),2.f));
  S.SetPressed(S.Hovered);S.SetDisabled(Empty);S.SetNormalPadding(FMargin(0));S.SetPressedPadding(FMargin(0));B->SetStyle(S);
 }
 int32 GalleryIndex=0;
 for(int32 I=0;I<GalleryButtons.Num();++I)
 {
  const bool Selected=SelectedGallery==GalleryIds[I];if(Selected)GalleryIndex=I;
  GalleryButtons[I]->SetStyle(ButtonStyle(Selected));
  GalleryLabels[I]->SetColorAndOpacity(FSlateColor(Selected?FLinearColor::White:FLinearColor(0.6f,0.72f,0.8f)));
 }
 if(SelectionTitle)SelectionTitle->SetText(FText::FromString(SelectedNumber?Names[SelectedNumber-1]:(GalleryIndex?GalleryNames[GalleryIndex]:TEXT("전시물을 선택해주세요"))));
 if(SelectionSubtitle)SelectionSubtitle->SetText(FText::FromString(SelectedNumber?TEXT("선택 완료 · 안내 버튼을 눌러주세요"):(GalleryIndex?TEXT("강조된 전시관의 화석을 골라주세요"):TEXT("지도 위 화석을 눌러보세요"))));
 if(GuideButton)GuideButton->SetIsEnabled(SelectedNumber!=0 && !bClosing);
 if(LexiTitle)
 {
  const FString Intro=SelectedNumber==2?FString(TEXT("여기가 박물관 입구예요")):
   (SelectedNumber==12?FString(TEXT("여기가 박물관 출구예요")):
   (SelectedNumber?FString::Printf(TEXT("%s 전시 공간이에요"),Names[SelectedNumber-1]):
   (GalleryIndex?FString::Printf(TEXT("여기는 %s이에요"),GalleryNames[GalleryIndex]):FString(TEXT("어디로 가볼까요?")))));
  LexiTitle->SetText(FText::FromString(Intro));
 }
 if(LexiDescription)LexiDescription->SetText(FText::FromString(SelectedNumber?
  SpecimenDescriptions[SelectedNumber-1]:GalleryDescriptions[GalleryIndex]));
}
int32 UNavFullMapWidget::NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled) const
{
 const int32 Top=Super::NativePaint(Args,Geometry,Clip,Out,Layer,Style,Enabled);
 if(!ReferenceCanvas || SelectedGallery==TEXT("all"))return Top;
 const FGeometry& CG=ReferenceCanvas->GetCachedGeometry();if(CG.GetLocalSize().X<=0)return Top;
 auto Local=[&](FVector2D P){return Geometry.AbsoluteToLocal(CG.LocalToAbsolute(FVector2D(MapX+P.X*MapScale,MapY+P.Y*MapScale)));};
 const float PixelScale=(Local({1,0})-Local({0,0})).Size();const FPaintGeometry Paint=Geometry.ToPaintGeometry();
 // Scanline fill follows concave walls rather than filling their bounding boxes.
 for(const TArray<FVector2D>& Polygon:GalleryPolygons(SelectedGallery))
 {
  float MinY=MAX_flt,MaxY=-MAX_flt;
  for(const FVector2D& P:Polygon){MinY=FMath::Min(MinY,float(P.Y));MaxY=FMath::Max(MaxY,float(P.Y));}
  const float Step=1.f/FMath::Max(PixelScale,0.1f);
  for(float Y=MinY+Step*.5f;Y<MaxY;Y+=Step)
  {
   TArray<float> Crossings;
   for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
   {
    const FVector2D A=Polygon[I],B=Polygon[J];
    if((A.Y<=Y && B.Y>Y)||(B.Y<=Y && A.Y>Y))Crossings.Add(A.X+(Y-A.Y)*(B.X-A.X)/(B.Y-A.Y));
   }
   Crossings.Sort();
   for(int32 I=0;I+1<Crossings.Num();I+=2)
   {
    const FVector2D Start=Local({Crossings[I],Y-Step*.5f});
    const FVector2D End=Local({Crossings[I+1],Y+Step*.5f});
    // Filled adjacent spans scale with UMG DPI. Thin Slate lines leave gaps on Android.
    FSlateDrawElement::MakeBox(Out,Top+1,Geometry.ToPaintGeometry(FVector2f(End-Start),
     FSlateLayoutTransform(FVector2f(Start))),FCoreStyle::Get().GetBrush("WhiteBrush"),
     ESlateDrawEffect::None,FLinearColor(0.12f,0.77f,0.95f,0.10f));
   }
  }
  TArray<FVector2D> Outline;for(const FVector2D& P:Polygon)Outline.Add(Local(P));
  const FVector2D FirstPoint=Outline[0];Outline.Add(FirstPoint);
  FSlateDrawElement::MakeLines(Out,Top+2,Paint,Outline,ESlateDrawEffect::None,FLinearColor(0.18f,0.85f,1.f,0.85f),true,2.5f);
 }
 return Top+2;
}
