#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NavMinimapWidget.h"
#include "NavFullMapWidget.generated.h"

class UBorder;
class UTextBlock;
class UPanelWidget;
class UNavDestButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class USizeBox;
class UTexture2D;
class UButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNavFullMapClosed);

/** Supplied floor plan with gallery previews and explicit destination confirmation.
 * Bound MapView carries real navigation data independently of illustration coordinates.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UNavFullMapWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
  bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
  const FString& InDestNodeId);

 /** Refresh live destinations without discarding an unconfirmed user selection. */
 void RefreshDestinations(const FNavGraph& InGraph, const FString& InDestNodeId);

 UPROPERTY(BlueprintAssignable, Category="Nav|FullMap")
 FOnNavDestinationChosen OnDestinationChosen;
 UPROPERTY(BlueprintAssignable, Category="Nav|FullMap")
 FOnNavFullMapClosed OnClosed;

 UFUNCTION(BlueprintCallable, Category="Nav|FullMap")
 void Close();

 UNavMinimapWidget* GetMapView() const { return MapView; }

 /** Source PNG coordinates (429 x 555). Image coordinates never change metric routing. */
 bool FindReferenceDestinationAtLocal(const FVector2D& SourceImagePosition, FString& OutNodeId) const;
 bool UsesReferenceArtwork() const;
 /** Preview is deliberately separate from starting guidance. */
 void SelectDestination(const FString& NodeId);
 void SelectGallery(const FString& GalleryId);
 UFUNCTION()
 void StartSelectedGuidance();
 FString GetSelectedDestination() const { return SelectedDestination; }
 FString GetSelectedGallery() const { return SelectedGallery; }

protected:
 virtual void NativeConstruct() override;
 virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
 virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
  const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
  const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

 UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="Nav|FullMap")
 TObjectPtr<UNavMinimapWidget> MapView;
 UPROPERTY(BlueprintReadOnly, meta=(BindWidget), Category="Nav|FullMap")
 TObjectPtr<UBorder> Backdrop;
 UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Nav|FullMap")
 TObjectPtr<UTextBlock> TitleText;
 UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Nav|FullMap")
 TObjectPtr<UPanelWidget> DestButtonHost;

private:
 TMap<int32,FString> ReferenceNodeIds;
 TArray<int32> DestinationButtonNumbers;
 UPROPERTY(Transient)
 TObjectPtr<UTexture2D> ReferenceTexture;
 UPROPERTY(Transient)
 TObjectPtr<UCanvasPanel> ReferenceCanvas;
 UPROPERTY(Transient)
 TObjectPtr<UButton> GuideButton;
 UPROPERTY(Transient)
 TObjectPtr<UCanvasPanelSlot> BackLayoutSlot;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> BackLabel;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> LexiTitle;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> LexiDescription;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> SelectionTitle;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> SelectionSubtitle;
 UPROPERTY(Transient)
 TArray<TObjectPtr<UNavDestButton>> GalleryButtons;
 UPROPERTY(Transient)
 TArray<TObjectPtr<UTextBlock>> GalleryLabels;
 FString SelectedDestination;
 FString SelectedGallery = TEXT("all");
 void UpdateSelectionAppearance();
 UFUNCTION()
 void HandleGalleryClicked(const FString& GalleryId);
 void BuildSkinLayout();
 void BuildDestinationButtons(const FNavGraph& InGraph);
 UFUNCTION()
 void HandleDestButtonClicked(const FString& NodeId);

 bool bClosing=false;
 float MapWaitSeconds=0.f;
 FString ActiveDestination;
 UPROPERTY(Transient)
 TObjectPtr<UTextBlock> MapStatus;
 UPROPERTY(Transient)
 TArray<TObjectPtr<UNavDestButton>> DestButtons;
};
