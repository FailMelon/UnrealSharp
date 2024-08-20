// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "FunctionsExporter.h"
#include "FPackageNameExporter.generated.h"

UCLASS(meta = (NotGeneratorValid))
class CSHARPFORUE_API UFPackageNameExporter : public UFunctionsExporter
{
	GENERATED_BODY()

public:

	// UFunctionsExporter interface implementation
	virtual void ExportFunctions(FRegisterExportedFunction RegisterExportedFunction) override;

	// End

private:

	static void RegisterMountPoint(TCHAR* RootPath, TCHAR* ContentPath);
	static void UnRegisterMountPoint(TCHAR* RootPath, TCHAR* ContentPath);	
};
