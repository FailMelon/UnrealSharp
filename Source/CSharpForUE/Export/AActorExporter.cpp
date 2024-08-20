#include "AActorExporter.h"
#include "CSManager.h"
#include "Components/InputComponent.h"

void UAActorExporter::ExportFunctions(FRegisterExportedFunction RegisterExportedFunction)
{
	EXPORT_FUNCTION(GetRootComponent);
	EXPORT_FUNCTION(FinishSpawning);
}

void* UAActorExporter::GetRootComponent(AActor* Actor)
{
	if (!IsValid(Actor))
	{
		return nullptr;
	}
	
	return FCSManager::Get().FindManagedObject(Actor->GetRootComponent()).GetIntPtr();
}

void UAActorExporter::FinishSpawning(AActor* Actor)
{
	Actor->FinishSpawning(Actor->GetTransform(), true);
}