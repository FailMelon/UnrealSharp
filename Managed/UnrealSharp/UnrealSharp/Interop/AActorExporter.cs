namespace UnrealSharp.Interop;

[NativeCallbacks]
public static unsafe partial class AActorExporter
{
    //static void* FinishSpawning(AActor* Actor);
    public static delegate* unmanaged<IntPtr, void> FinishSpawning;
}