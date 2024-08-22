#include "FPackageNameExporter.h"
#include "Misc/PackageName.h"


void UFPackageNameExporter::ExportFunctions(FRegisterExportedFunction RegisterExportedFunction)
{
	EXPORT_FUNCTION(RegisterMountPoint);
	EXPORT_FUNCTION(UnRegisterMountPoint);
}

void UFPackageNameExporter::RegisterMountPoint(TCHAR* RootPath, TCHAR* ContentPath)
{
	FPackageName::RegisterMountPoint(RootPath, ContentPath);
}

void UFPackageNameExporter::UnRegisterMountPoint(TCHAR* RootPath, TCHAR* ContentPath)
{
	FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
}
