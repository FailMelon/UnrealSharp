using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace UnrealSharp.Interop;


[NativeCallbacks]
public static unsafe partial class FPackageNameExporter
{
    public static delegate* unmanaged<char*, char*, void> RegisterMountPoint;
    public static delegate* unmanaged<char*, char*, void> UnRegisterMountPoint;
}
