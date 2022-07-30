﻿// Copyright 2022, Aquanox.

#include "K2Node_CreateBlueprintMessage.h"
#include "BlueprintMessage.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "K2Node_MakeArray.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "ToolMenu.h"
#include "Kismet2/BlueprintEditorUtils.h"

#define LOCTEXT_NAMESPACE "BlueprintMesage"

constexpr bool REFERENCE_INPUT = false;

UK2Node_CreateBlueprintMessage::UK2Node_CreateBlueprintMessage()
{
	NumInputs = 1;
	FunctionReference.SetExternalMember(
		GET_FUNCTION_NAME_CHECKED(UBlueprintMessage, CreateBlueprintMessage),
		UBlueprintMessage::StaticClass()
	);
}

void UK2Node_CreateBlueprintMessage::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	for (int32 Index = 0; Index < NumInputs; ++Index)
	{
		FCreatePinParams PinParams;
		PinParams.bIsReference = REFERENCE_INPUT;

		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, NAME_None, FBlueprintMessageToken::StaticStruct(), GetPinName(Index), PinParams);
	}
}

void UK2Node_CreateBlueprintMessage::PostReconstructNode()
{
	Super::PostReconstructNode();
}

FText UK2Node_CreateBlueprintMessage::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return Super::GetNodeTitle(TitleType);
}

FName UK2Node_CreateBlueprintMessage::GetPinName(int32 PinIndex) const
{
	return *FString::Printf(TEXT("[%d]"), PinIndex);
}

bool UK2Node_CreateBlueprintMessage::IsDynamicInputPin(const UEdGraphPin* Pin) const
{
	return Pin
		&& Pin->Direction == EGPD_Input
		&& Pin->ParentPin == nullptr
		&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct
		&& Pin->PinType.PinSubCategoryObject == FBlueprintMessageToken::StaticStruct();
}

void UK2Node_CreateBlueprintMessage::SyncPinNames()
{
	int32 CurrentNumParentPins = 0;
	for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
	{
		UEdGraphPin*& CurrentPin = Pins[PinIndex];
		if (IsDynamicInputPin(CurrentPin))
		{
			const FName OldName = CurrentPin->PinName;
			const FName ElementName = GetPinName(CurrentNumParentPins++);

			CurrentPin->Modify();
			CurrentPin->PinName = ElementName;

			if (CurrentPin->SubPins.Num() > 0)
			{
				const FString OldNameStr = OldName.ToString();
				const FString ElementNameStr = ElementName.ToString();
				FString OldFriendlyName = OldNameStr;
				FString ElementFriendlyName = ElementNameStr;

				// SubPin Friendly Name has an extra space in it so we need to account for that
				OldFriendlyName.InsertAt(1, " ");
				ElementFriendlyName.InsertAt(1, " ");

				for (UEdGraphPin* SubPin : CurrentPin->SubPins)
				{
					FString SubPinFriendlyName = SubPin->PinFriendlyName.ToString();
					SubPinFriendlyName.ReplaceInline(*OldFriendlyName, *ElementFriendlyName);

					SubPin->Modify();
					SubPin->PinName = *SubPin->PinName.ToString().Replace(*OldNameStr, *ElementNameStr);
					SubPin->PinFriendlyName = FText::FromString(SubPinFriendlyName);
				}
			}
		}
	}
}

void UK2Node_CreateBlueprintMessage::InteractiveAddInputPin()
{
	FScopedTransaction Transaction(NSLOCTEXT("BlueprintMessage", "AddPinTx", "Add Pin"));
	AddInputPin();
}

void UK2Node_CreateBlueprintMessage::AddInputPin()
{
	Modify();

	++NumInputs;

	FCreatePinParams PinParams;
	PinParams.bIsReference = REFERENCE_INPUT;

	UEdGraphPin* Pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, NAME_None, FBlueprintMessageToken::StaticStruct(), GetPinName(NumInputs-1), PinParams);
	GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValueBasedOnType(Pin);

	const bool bIsCompiling = GetBlueprint()->bBeingCompiled;
	if( !bIsCompiling )
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}
}

void UK2Node_CreateBlueprintMessage::RemoveInputPin(UEdGraphPin* Pin)
{
	check(Pin->Direction == EGPD_Input);
	check(Pin->ParentPin == nullptr);
	checkSlow(Pins.Contains(Pin));

	FScopedTransaction Transaction(NSLOCTEXT("BlueprintMessage", "RemovePinTx", "RemovePin"));
	Modify();

	TFunction<void(UEdGraphPin*)> RemovePinLambda = [this, &RemovePinLambda](UEdGraphPin* PinToRemove)
	{
		for (int32 SubPinIndex = PinToRemove->SubPins.Num()-1; SubPinIndex >= 0; --SubPinIndex)
		{
			RemovePinLambda(PinToRemove->SubPins[SubPinIndex]);
		}

		int32 PinRemovalIndex = INDEX_NONE;
		if (Pins.Find(PinToRemove, PinRemovalIndex))
		{
			Pins.RemoveAt(PinRemovalIndex);
			PinToRemove->MarkAsGarbage();
		}
	};

	RemovePinLambda(Pin);
	PinConnectionListChanged(Pin);

	--NumInputs;
	SyncPinNames();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
}

FText UK2Node_CreateBlueprintMessage::GetMenuCategory() const
{
	return Super::GetMenuCategory();
}

void UK2Node_CreateBlueprintMessage::GetMenuActions(FBlueprintActionDatabaseRegistrar& InActionRegistrar) const
{
	const UClass* ActionKey = GetClass();
	if (InActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);
		InActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_CreateBlueprintMessage::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);

	if (!Context->bIsDebugging)
	{
		FToolMenuSection& Section = Menu->AddSection("EdGraphSchemaPinActions", LOCTEXT( "CreateBlueprintMessageHeader", "Message Token"));

		if (Context->Pin != NULL)
		{
			if (IsDynamicInputPin(Context->Pin))
			{
				Section.AddMenuEntry(
					"RemovePin",
					LOCTEXT("CreateBlueprintMessage_RemovePin", "Remove Token Pin"),
					LOCTEXT("CreateBlueprintMessage_RemovePinTooltip", "Remove this token pin"),
					FSlateIcon(),
					FUIAction(
						FExecuteAction::CreateUObject(const_cast<ThisClass*>(this), &ThisClass::RemoveInputPin, const_cast<UEdGraphPin*>(Context->Pin))
					)
				);
			}
		}
		else
		{
			Section.AddMenuEntry(
				"AddPin",
				LOCTEXT("CreateBlueprintMessage_AddPin", "Add Token Pin"),
				LOCTEXT("CreateBlueprintMessage_AddPinTooltip", "Add another token pin"),
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateUObject(const_cast<ThisClass*>(this), &ThisClass::InteractiveAddInputPin)
				)
			);
		}
	}
}

void UK2Node_CreateBlueprintMessage::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	TArray<UEdGraphPin*> DynamicPins;
	for (UEdGraphPin* Pin : Pins)
	{
		if (IsDynamicInputPin(Pin))
		{
			DynamicPins.Add(Pin);
		}
	}

	if (!DynamicPins.Num())
	{ // there are no dynamic pins present. just do regular function call
		Super::ExpandNode(CompilerContext, SourceGraph);
		return;
	}

	bool bIsErrorFree = true;

	auto MovePinLinksToIntermediate = [&](UK2Node* From, FName PinName, UK2Node* To, FName ToPinName)
	{
		UEdGraphPin* SourcePin = From->FindPinChecked(PinName);
		UEdGraphPin* DestPin = To->FindPinChecked(ToPinName);
		bool bSuccess = CompilerContext.MovePinLinksToIntermediate(*SourcePin, *DestPin).CanSafeConnect();
		check(bSuccess);
		return bSuccess;
	};

	// Create a "Create Message" node
	UK2Node_CallFunction* CreateNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CreateNode->FunctionReference = FunctionReference;
	CreateNode->AllocateDefaultPins();

	bIsErrorFree &= MovePinLinksToIntermediate(this, UEdGraphSchema_K2::PN_Execute, CreateNode, UEdGraphSchema_K2::PN_Execute);
	bIsErrorFree &= MovePinLinksToIntermediate(this, TEXT("LogCategory"), CreateNode, TEXT("LogCategory"));
	bIsErrorFree &= MovePinLinksToIntermediate(this, TEXT("Severity"), CreateNode, TEXT("Severity"));

	// Create a "Add Tokens" node
	UK2Node_CallFunction* AddTokensNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	AddTokensNode->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UBlueprintMessage, AddTokens), UBlueprintMessage::StaticClass());
	AddTokensNode->AllocateDefaultPins();

	// Connect Target to result of Create Message
	AddTokensNode->FindPinChecked(UEdGraphSchema_K2::PN_Self)->MakeLinkTo(CreateNode->GetReturnValuePin());
	// Connect Execute to Then of Create Message
	CreateNode->GetThenPin()->MakeLinkTo(AddTokensNode->FindPinChecked(UEdGraphSchema_K2::PN_Execute));

	// Create a "Make Array" node
	UK2Node_MakeArray* MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeArrayNode->NumInputs = DynamicPins.Num();
	MakeArrayNode->AllocateDefaultPins();

	// Connect the output of the "Make Array" pin to "Tokens"
	// PinConnectionListChanged will set the "Make Array" node's type, only works if one pin is connected
	UEdGraphPin* ArrayOut = MakeArrayNode->GetOutputPin();
	ArrayOut->MakeLinkTo(AddTokensNode->FindPinChecked(TEXT("Tokens")));
	// AddTokensNode->FindPinChecked(TEXT("Tokens"))->MakeLinkTo(ArrayOut);
	MakeArrayNode->PinConnectionListChanged(ArrayOut);

	// Transfer dynamic pins to "Make Array"
	for (int32 Index = 0; Index < DynamicPins.Num(); ++Index)
	{
		// Find the input pin on the "Make Array" node by index and link it to the literal string
		UEdGraphPin* ArrayIn = MakeArrayNode->FindPinChecked(FString::Printf(TEXT("[%d]"), Index));

		bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*DynamicPins[Index], *ArrayIn).CanSafeConnect();
	}

	// Connect Then
	MovePinLinksToIntermediate(this, UEdGraphSchema_K2::PN_Then, AddTokensNode, UEdGraphSchema_K2::PN_Then);
	// Connect Return Value
	MovePinLinksToIntermediate(this, UEdGraphSchema_K2::PN_ReturnValue, CreateNode, UEdGraphSchema_K2::PN_ReturnValue);

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("InternalConnectionError", "BaseAsyncTask: Internal connection error. @@").ToString(), this);
	}

	// orphan current node
	BreakAllNodeLinks();
}

#undef LOCTEXT_NAMESAPCE
