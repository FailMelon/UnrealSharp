#include "CSCompilerContext.h"

#include "ComponentTypeRegistry.h"
#include "ISettingsModule.h"
#include "BehaviorTree/Tasks/BTTask_BlueprintBase.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Types/CSBlueprint.h"
#include "Types/CSClass.h"
#include "Types/CSSkeletonClass.h"
#include "Factories/CSFunctionFactory.h"
#include "Factories/CSPropertyFactory.h"
#include "Utilities/CSMetaDataUtils.h"
#include "CSUnrealSharpEditorSettings.h"
#include "UnrealSharpUtils.h"
#include "BehaviorTree/Decorators/BTDecorator_BlueprintBase.h"
#include "BehaviorTree/Services/BTService_BlueprintBase.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Blueprint/StateTreeConsiderationBlueprintBase.h"
#include "Compilers/CSManagedClassCompiler.h"
#include "Compilers/CSSimpleConstructionScriptCompiler.h"
#include "HotReload/CSHotReloadSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReflectionData/CSClassReflectionData.h"
#include "UObject/PropertyAccessUtil.h"
#include "UObject/UObjectHash.h"
#include "Utilities/CSClassUtilities.h"

FCSCompilerContext::FCSCompilerContext(UCSBlueprint* Blueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompilerOptions) : FKismetCompilerContext(Blueprint, InMessageLog, InCompilerOptions)
{
}

void FCSCompilerContext::FinishCompilingClass(UClass* InClass)
{
	UCSClass* ManagedClass = static_cast<UCSClass*>(InClass);
	TSharedPtr<FCSClassReflectionData> ClassReflectionData = GetClassInfo()->GetReflectionData<FCSClassReflectionData>();
	
	UClass* ParentClass = ManagedClass->GetSuperClass();
	FBlueprintEditorUtils::RecreateClassMetaData(Blueprint, ManagedClass, false);
	
	ManagedClass->ReferenceSchema.Reset();
	UCSManagedClassCompiler::SetClassFlags(ManagedClass, ClassReflectionData);
	UCSManagedClassCompiler::SetConfigName(ManagedClass, ClassReflectionData);
		
	if (ParentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		FComponentTypeRegistry::Get().InvalidateClass(ManagedClass);
	}
	
	ManagedClass->ClassFlags |= ClassReflectionData->ClassFlags;
	
	ManagedClass->SimpleConstructionScript = Blueprint->SimpleConstructionScript;
	ManagedClass->InheritableComponentHandler = Blueprint->InheritableComponentHandler;
	ManagedClass->ComponentClassOverrides = Blueprint->ComponentClassOverrides;
	
	ManagedClass->ClassConstructor = &UCSClass::ManagedObjectConstructor;
	ManagedClass->Bind();

	ManagedClass->StaticLink(true);
	
	UCSManagedClassCompiler::CreateDeferredManagedCDO(ManagedClass);

	if (FCSClassUtilities::IsSkeletonType(ManagedClass) || FCSUnrealSharpUtils::IsEngineStartingUp())
	{
		// Fast skeleton generation doesn't propagate term defaults, so it must be finalized here.
		// Doing this on every compile keeps hot reload consistent with editor startup.
		UCSManagedClassCompiler::FinalizeManagedCDO(ManagedClass);
	}
	
	ManagedClass->SetUpRuntimeReplicationData();
	ManagedClass->UpdateCustomPropertyListForPostConstruction();
	
	ManagedClass->InitializeFieldNotifies();
	
	TryInitializeAsDeveloperSettings(ManagedClass);
	TryFakeNativeClass(ManagedClass);
	
	ApplyMetaData();
}

void FCSCompilerContext::OnPostCDOCompiled(const UObject::FPostCDOCompiledContext& Context)
{
	FKismetCompilerContext::OnPostCDOCompiled(Context);

	UCSClass* MainClass = GetMainClass();
	if (MainClass == NewClass)
	{
		UCSManagedClassCompiler::ActivateSubsystem(NewClass);
		UCSManagedClassCompiler::RefreshClassActions(NewClass);
	}
	
	UCSManagedClassCompiler::SetupDefaultTickSettings(NewClass->GetDefaultObject(), NewClass);
}

void FCSCompilerContext::CreateClassVariablesFromBlueprint()
{
	TSharedPtr<FCSManagedTypeDefinition> ManagedTypeDefinition = GetMainClass()->GetManagedTypeDefinition();
	TSharedPtr<FCSClassReflectionData> ClassReflectionData = ManagedTypeDefinition->GetReflectionData<FCSClassReflectionData>();
	const TArray<FCSPropertyReflectionData>& PropertiesReflectionData = ClassReflectionData->Properties;

	NewClass->PropertyGuids.Empty(PropertiesReflectionData.Num());
	FCSPropertyFactory::CreateAndAssignProperties(NewClass, PropertiesReflectionData, [this](const FProperty* NewProperty)
	{
		FName PropertyName = NewProperty->GetFName();
		FGuid PropertyGuid = FCSUnrealSharpUtils::ConstructGUIDFromName(PropertyName);
		NewClass->PropertyGuids.Add(PropertyName, PropertyGuid);
	});
	
	ValidateSimpleConstructionScript();

	// Create dummy variables for the blueprint.
	// They should not get compiled, just there for metadata for different Unreal modules.
	CreateDummyBlueprintVariables(PropertiesReflectionData);
}

void FCSCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOldCDO)
{
	FKismetCompilerContext::CleanAndSanitizeClass(ClassToClean, InOldCDO);
	NewClass->FieldNotifies.Reset();

	TryDeinitializeAsDeveloperSettings(InOldCDO);

	// Too late to generate functions in CreateFunctionList for child blueprints
	GenerateFunctions();
}

void FCSCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	UCSClass* MainClass = GetMainClass();
	
	UCSSkeletonClass* NewSkeletonClass = NewObject<UCSSkeletonClass>(Blueprint->GetOutermost(), *NewClassName, RF_Public | RF_Transactional);
	NewSkeletonClass->SetOwningBlueprint(Blueprint);
	NewSkeletonClass->SetGeneratedClass(MainClass);

	ICSManagedTypeInterface* ManagedType = FCSClassUtilities::GetManagedType(NewSkeletonClass);
	ManagedType->SetManagedTypeDefinition(MainClass->GetManagedTypeDefinition());

	Blueprint->SkeletonGeneratedClass = NewSkeletonClass;
	NewClass = NewSkeletonClass;

	// Skeleton class doesn't generate functions on the first pass.
	// It's done in CleanAndSanitizeClass which doesn't run when the skeleton class is created
	GenerateFunctions();
}

void FCSCompilerContext::AddInterfacesFromBlueprint(UClass* Class)
{
	UCSManagedClassCompiler::ImplementInterfaces(Class, GetReflectionData()->Interfaces);
}

void FCSCompilerContext::CopyTermDefaultsToDefaultObject(UObject* DefaultObject)
{
	UCSClass* ManagedClass = static_cast<UCSClass*>(DefaultObject->GetClass());
	UCSManagedClassCompiler::FinalizeManagedCDO(ManagedClass);
	FKismetCompilerContext::CopyTermDefaultsToDefaultObject(DefaultObject);

	// The compilation manager moves the previous CDO onto OldClass (the REINST_ duplicate)
	// before the deferred CDO propagation pass. OldCDO is therefore normally null here for
	// batched editor compiles, while direct compiler invocations still populate it.
	UObject* PreviousCDO = IsValid(OldClass) ? OldClass->GetDefaultObject(false) : nullptr;
	if (!IsValid(PreviousCDO))
	{
		PreviousCDO = OldCDO;
	}
	if (!IsValid(PreviousCDO)
		|| DefaultObject->GetClass() != GetMainClass()
		|| FCSUnrealSharpUtils::IsEngineStartingUp())
	{
		return;
	}

	UClass* CompiledClass = DefaultObject->GetClass();
	UClass* PreviousClass = PreviousCDO->GetClass();
	for (TFieldIterator<FProperty> PropertyIt(CompiledClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		FProperty* NewProperty = *PropertyIt;
		FProperty* OldProperty = PreviousClass->FindPropertyByName(NewProperty->GetFName());
		if (!OldProperty
			|| !NewProperty->HasAnyPropertyFlags(CPF_Edit)
			|| NewProperty->ContainsInstancedObjectProperty()
			|| OldProperty->ContainsInstancedObjectProperty()
			|| !PropertyAccessUtil::ArePropertiesCompatible(OldProperty, NewProperty))
		{
			continue;
		}

		const void* OldValue = OldProperty->ContainerPtrToValuePtr<void>(PreviousCDO);
		const void* NewValue = NewProperty->ContainerPtrToValuePtr<void>(DefaultObject);
		if (PropertyAccessUtil::IsCompletePropertyIdentical(OldProperty, OldValue, NewProperty, NewValue))
		{
			continue;
		}

		// Reinstancing copies old object values after running the new constructor. Move only values
		// that still match their old default before that copy, preserving authored overrides.
		UpdateInstancesWithInheritedDefault(PreviousClass, OldProperty, OldValue, NewProperty, NewValue);
		PropagateDefaultToBlueprintChildren(CompiledClass, OldProperty, OldValue, NewProperty, NewValue);
	}
}

void FCSCompilerContext::UpdateInstancesWithInheritedDefault(
	const UClass* InstanceClass,
	const FProperty* OldDefaultProperty,
	const void* OldDefaultValue,
	const FProperty* NewDefaultProperty,
	const void* NewDefaultValue)
{
	TArray<UObject*> Instances;
	GetObjectsOfClass(InstanceClass, Instances, false, RF_ClassDefaultObject);

	for (UObject* Instance : Instances)
	{
		if (!IsValid(Instance) || Instance->GetOutermost() == GetTransientPackage())
		{
			continue;
		}

		FProperty* InstanceProperty = Instance->GetClass()->FindPropertyByName(NewDefaultProperty->GetFName());
		if (!InstanceProperty)
		{
			continue;
		}

		void* InstanceValue = InstanceProperty->ContainerPtrToValuePtr<void>(Instance);
		if (PropertyAccessUtil::IsCompletePropertyIdentical(OldDefaultProperty, OldDefaultValue, InstanceProperty, InstanceValue))
		{
			PropertyAccessUtil::CopyCompletePropertyValue(NewDefaultProperty, NewDefaultValue, InstanceProperty, InstanceValue);
		}
	}
}

void FCSCompilerContext::PropagateDefaultToBlueprintChildren(
	UClass* ParentClass,
	const FProperty* OldParentProperty,
	const void* OldParentValue,
	const FProperty* NewParentProperty,
	const void* NewParentValue)
{
	TArray<UClass*> ChildClasses;
	GetDerivedClasses(ParentClass, ChildClasses, false);

	for (UClass* ChildClass : ChildClasses)
	{
		if (!IsValid(ChildClass) || ChildClass->HasAnyClassFlags(CLASS_NewerVersionExists) || FCSClassUtilities::IsManagedClass(ChildClass))
		{
			continue;
		}

		const UBlueprint* ChildBlueprint = Cast<UBlueprint>(ChildClass->ClassGeneratedBy);
		if (!ChildBlueprint || ChildBlueprint->GeneratedClass != ChildClass)
		{
			continue;
		}

		UObject* ChildCDO = ChildClass->GetDefaultObject(false);
		if (!IsValid(ChildCDO))
		{
			continue;
		}

		// A layout-changing compile can temporarily leave the old CDO on the new class.
		// Always use the CDO's actual class to address its current property storage safely.
		FProperty* ChildProperty = ChildCDO->GetClass()->FindPropertyByName(NewParentProperty->GetFName());
		if (!ChildProperty || !PropertyAccessUtil::ArePropertiesCompatible(NewParentProperty, ChildProperty))
		{
			continue;
		}

		void* ChildValue = ChildProperty->ContainerPtrToValuePtr<void>(ChildCDO);
		void* OldChildValue = ChildProperty->ContainerPtrToValuePtr<void>(ChildCDO);
		void* NewChildValue = ChildProperty->ContainerPtrToValuePtr<void>(ChildCDO);
		PropertyAccessUtil::CopyCompletePropertyValue(ChildProperty, ChildValue, ChildProperty, OldChildValue);
		PropertyAccessUtil::CopyCompletePropertyValue(ChildProperty, ChildValue, ChildProperty, NewChildValue);

		const bool bInheritedFromParent = PropertyAccessUtil::IsCompletePropertyIdentical(
			OldParentProperty,
			OldParentValue,
			ChildProperty,
			OldChildValue);

		if (bInheritedFromParent
			&& PropertyAccessUtil::CopyCompletePropertyValue(NewParentProperty, NewParentValue, ChildProperty, NewChildValue)
			&& !PropertyAccessUtil::IsCompletePropertyIdentical(ChildProperty, OldChildValue, ChildProperty, NewChildValue))
		{
			UpdateInstancesWithInheritedDefault(
				ChildCDO->GetClass(),
				ChildProperty,
				OldChildValue,
				ChildProperty,
				NewChildValue);

			PropertyAccessUtil::CopyCompletePropertyValue(ChildProperty, NewChildValue, ChildProperty, ChildValue);
		}

		// Carry both layers forward. A child override becomes the baseline for its own children,
		// while an inherited value carries the parent's new default through the hierarchy.
		PropagateDefaultToBlueprintChildren(
			ChildClass,
			ChildProperty,
			OldChildValue,
			ChildProperty,
			NewChildValue);
	}
}

void FCSCompilerContext::ValidateSimpleConstructionScript() const
{
	UCSClass* MainClass = GetMainClass();
	
	if (MainClass == NewClass)
	{
		// This is a bit weird, but we create the main class' SCS from the skeleton class compilation, since we need it for the skeleton's managed constructor.
		// And it's not allowed to have SCS on the skeleton class.
		return;
	}
	
	const TArray<FCSPropertyReflectionData>& Properties = GetReflectionData()->Properties;
	
	FCSSimpleConstructionScriptCompiler::CompileSimpleConstructionScript(MainClass, &MainClass->SimpleConstructionScript, Properties);
	USimpleConstructionScript* SimpleConstructionScript = MainClass->SimpleConstructionScript;
	Blueprint->SimpleConstructionScript = SimpleConstructionScript;

	if (!IsValid(SimpleConstructionScript))
	{
		return;
	}

	TArray<USCS_Node*> Nodes;
	for (const FCSPropertyReflectionData& Property : Properties)
	{
		if (Property.InnerType->PropertyType != ECSPropertyType::DefaultComponent)
		{
			continue;
		}
		
		USCS_Node* Node = SimpleConstructionScript->FindSCSNode(Property.GetName());
		Nodes.Add(Node);
	}

	// Remove all nodes that are not part of the class anymore.
	int32 NumNodes = SimpleConstructionScript->GetAllNodes().Num();
	TArray<USCS_Node*> AllNodes = SimpleConstructionScript->GetAllNodes();
	for (int32 i = NumNodes - 1; i >= 0; --i)
	{
		USCS_Node* Node = AllNodes[i];
		if (!Nodes.Contains(Node))
		{
			SimpleConstructionScript->RemoveNode(Node);
		}
	}

	SimpleConstructionScript->ValidateNodeTemplates(MessageLog);
	SimpleConstructionScript->ValidateNodeVariableNames(MessageLog);
}

void FCSCompilerContext::GenerateFunctions() const
{
	TSharedPtr<const FCSClassReflectionData> ReflectionData = GetReflectionData();
	FCSFunctionFactory::GenerateVirtualFunctions(NewClass, ReflectionData);
	FCSFunctionFactory::GenerateFunctions(NewClass, ReflectionData->Functions);
}

UCSClass* FCSCompilerContext::GetMainClass() const
{
	return CastChecked<UCSClass>(Blueprint->GeneratedClass);
}

TSharedPtr<const FCSManagedTypeDefinition> FCSCompilerContext::GetClassInfo() const
{
	return GetMainClass()->GetManagedTypeDefinition();
}

TSharedPtr<const FCSClassReflectionData> FCSCompilerContext::GetReflectionData() const
{
	return GetClassInfo()->GetReflectionData<FCSClassReflectionData>();
}

void FCSCompilerContext::TryInitializeAsDeveloperSettings(const UClass* Class) const
{
	if (!FCSClassUtilities::IsDeveloperSettingsClass(Blueprint, Class))
	{
		return;
	}

	UDeveloperSettings* Settings = static_cast<UDeveloperSettings*>(Class->GetDefaultObject());
	ISettingsModule& SettingsModule = FModuleManager::GetModuleChecked<ISettingsModule>("Settings");

	SettingsModule.RegisterSettings(Settings->GetContainerName(), Settings->GetCategoryName(),
	                                Settings->GetSectionName(),
	                                Settings->GetSectionText(),
	                                Settings->GetSectionDescription(),
	                                Settings);

	Settings->LoadConfig();
}

void FCSCompilerContext::TryDeinitializeAsDeveloperSettings(UObject* Settings) const
{
	if (!IsValid(Settings) || !FCSClassUtilities::IsDeveloperSettingsClass(Blueprint, NewClass))
	{
		return;
	}

	ISettingsModule& SettingsModule = FModuleManager::GetModuleChecked<ISettingsModule>("Settings");
	UDeveloperSettings* DeveloperSettings = static_cast<UDeveloperSettings*>(Settings);
	SettingsModule.UnregisterSettings(DeveloperSettings->GetContainerName(), DeveloperSettings->GetCategoryName(),
	                                  DeveloperSettings->GetSectionName());
}

void FCSCompilerContext::TryFakeNativeClass(UClass* Class)
{
	static TArray ParentClasses =
	{
		UBTTask_BlueprintBase::StaticClass(),
		UBTDecorator_BlueprintBase::StaticClass(),
		UBTService_BlueprintBase::StaticClass(),
		
		UStateTreeTaskBlueprintBase::StaticClass(),
		UStateTreeConditionBlueprintBase::StaticClass(),
		UStateTreeConsiderationBlueprintBase::StaticClass(),
	};

	bool bIsChildOfSpecialClass = false;
	for (UClass* ParentClass : ParentClasses)
	{
		if (Class->IsChildOf(ParentClass))
		{
			bIsChildOfSpecialClass = true;
			break;
		}
	}
	
	if (!bIsChildOfSpecialClass)
	{
		return;
	}

	// There are systems in Unreal (BehaviorTree, StateTree) which uses the AssetRegistry to find BP classes, since our C# classes are not assets,
	// we need to fake that they're native classes in editor in order to be able to find them. 

	// The functions that are used to find classes are:
	// FGraphNodeClassHelper::BuildClassGraph()
	// FStateTreeNodeClassCache::CacheClasses()
	
	// Ignore Skeleton classes, which otherwise may unintentionally show up in the systems reading from the AssetRegistry.
	// This happened for C# state tree tasks, showing both the Skeleton class and the regular class in the state tree task selection.
	if (Cast<UCSSkeletonClass>(Class))
	{
		return;
	}
	
	Class->ClassFlags |= CLASS_Native;
}

void FCSCompilerContext::ApplyMetaData() const
{
	static FString DisplayNameKey = TEXT("DisplayName");
	if (!NewClass->HasMetaData(*DisplayNameKey))
	{
		NewClass->SetMetaData(*DisplayNameKey, *Blueprint->GetName());
	}
	
	Blueprint->BlueprintDisplayName = NewClass->GetMetaData(*DisplayNameKey);

	if (GetDefault<UCSUnrealSharpEditorSettings>()->bSuffixGeneratedTypes)
	{
		FString DisplayName = NewClass->GetMetaData(*DisplayNameKey);
		DisplayName += TEXT(" (C#)");
		NewClass->SetMetaData(*DisplayNameKey, *DisplayName);
	}

	FCSMetaDataUtils::ApplyMetaData(GetReflectionData()->MetaData, NewClass);
}

void FCSCompilerContext::CreateDummyBlueprintVariables(const TArray<FCSPropertyReflectionData>& Properties) const
{
	Blueprint->NewVariables.Empty(Properties.Num());

	for (const FCSPropertyReflectionData& PropertyReflectionData : Properties)
	{
		FBPVariableDescription VariableDescription;
		VariableDescription.FriendlyName = PropertyReflectionData.GetName().ToString();
		VariableDescription.VarName = PropertyReflectionData.GetName();

		for (const FCSMetaDataEntry& MetaData : PropertyReflectionData.MetaData)
		{
			VariableDescription.SetMetaData(*MetaData.Key, MetaData.Value);
		}

		Blueprint->NewVariables.Add(VariableDescription);
	}
}

#undef LOCTEXT_NAMESPACE
