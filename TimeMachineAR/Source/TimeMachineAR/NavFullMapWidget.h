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

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnNavFullMapClosed);

/** Exact supplied artwork with native live-node marker and carousel hit surfaces.
 * Bound MapView carries real navigation data without painting over the reference.
 */
UCLASS(Abstract)
class TIMEMACHINEAR_API UNavFullMapWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 void ApplyState(const FNavGraph& InGraph, const TArray<FVector2D>& InRouteXY,
  bool bHasPose, const FVector2D& PoseXY, float PoseHeadingDeg, bool bPoseHasHeading,
  const FString& InDestNodeId);

 /** Refresh asynchronous graph cards without resetting carousel on pose updates. */
 void RefreshDestinations(const FNavGraph& InGraph, const FString& InDestNodeId);

 UPROPERTY(BlueprintAssignable, Category="Nav|FullMap")
 FOnNavDestinationChosen OnDestinationChosen;
 UPROPERTY(BlueprintAssignable, Category="Nav|FullMap")
 FOnNavFullMapClosed OnClosed;

 UFUNCTION(BlueprintCallable, Category="Nav|FullMap")
 void Close();

 UNavMinimapWidget* GetMapView() const { return MapView; }

 /** Source PNG coordinates (941 x 1672), including the omitted 50-pixel top strip. */
 bool FindReferenceDestinationAtLocal(const FVector2D& SourceImagePosition, FString& OutNodeId) const;
 bool UsesReferenceArtwork() const;

protected:
 virtual void NativeConstruct() override;
 virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

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
 TObjectPtr<UCanvasPanelSlot> HeaderLayoutSlot;
 UPROPERTY(Transient)
 TObjectPtr<USizeBox> HeaderDesignSize;
 UPROPERTY(Transient)
 TObjectPtr<UCanvasPanelSlot> MessageLayoutSlot;
 UPROPERTY(Transient)
 TObjectPtr<UCanvasPanelSlot> MessageTitleSlot;
 UPROPERTY(Transient)
 TObjectPtr<UCanvasPanelSlot> MessageSubtitleSlot;
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
