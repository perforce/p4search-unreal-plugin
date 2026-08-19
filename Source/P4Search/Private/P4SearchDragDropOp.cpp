// Copyright Perforce Software, Inc. All Rights Reserved.

#include "P4SearchDragDropOp.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"

TSharedRef<FP4SearchDragDropOp> FP4SearchDragDropOp::New(const FString& InFileName, TSharedPtr<FSlateBrush> InThumbnailBrush,
	FSimpleDelegate InOnDropped)
{
	TSharedRef<FP4SearchDragDropOp> Operation = MakeShared<FP4SearchDragDropOp>();
	Operation->FileName = InFileName;
	Operation->ThumbnailBrush = InThumbnailBrush;
	Operation->OnDropped = InOnDropped;
	Operation->Construct();
	return Operation;
}

TSharedPtr<SWidget> FP4SearchDragDropOp::GetDefaultDecorator() const
{
	return SNew(SBorder)
		.BorderBackgroundColor(FStyleColors::Panel)
		.Padding(8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SImage)
				.Image(ThumbnailBrush.Get())
				.DesiredSizeOverride(FVector2D(32.0f, 32.0f))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FileName))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FStyleColors::Foreground)
			]
		];
}

void FP4SearchDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	FDragDropOperation::OnDrop(bDropWasHandled, MouseEvent);
	OnDropped.ExecuteIfBound();
}
