using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealSharp.Interop;

namespace UnrealSharp.Extensions.Misc;

public partial class FPackageName
{
    public static void RegisterMountPoint(string RootPath, string ContentPath)
    {
        unsafe
        {
            fixed (char* rootPathPtr = RootPath)
            {
                fixed (char* contentPathPtr = ContentPath)
                {
                    FPackageNameExporter.CallRegisterMountPoint(rootPathPtr, contentPathPtr);
                }
            }
        }
    }

    public static void UnRegisterMountPoint(string RootPath, string ContentPath)
    {
        unsafe
        {
            fixed (char* rootPathPtr = RootPath)
            {
                fixed (char* contentPathPtr = ContentPath)
                {
                    FPackageNameExporter.CallUnRegisterMountPoint(rootPathPtr, contentPathPtr);
                }
            }
        }
    }
}
