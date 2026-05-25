using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace HoneyEngine;

[StructLayout(LayoutKind.Sequential)]
public unsafe struct NativeFunctionTable {
    public delegate* unmanaged<ulong, float*, void>          Entity_GetTranslation;
    public delegate* unmanaged<ulong, float*, void>          Entity_SetTranslation;
    public delegate* unmanaged<ulong, float*, void>          Entity_GetRotation;
    public delegate* unmanaged<ulong, float*, void>          Entity_SetRotation;
    public delegate* unmanaged<ulong, float*, void>          Entity_GetScale;
    public delegate* unmanaged<ulong, float*, void>          Entity_SetScale;
    public delegate* unmanaged<ulong, float, float, float, void> Rigidbody2D_ApplyLinearImpulse;
    public delegate* unmanaged<byte*, void>                  Log_Info;
    public delegate* unmanaged<byte*, void>                  Log_Warn;
    public delegate* unmanaged<byte*, void>                  Log_Error;
    public delegate* unmanaged<int, byte>                    Input_IsKeyDown;
}

public static unsafe class InternalCalls {
    internal static NativeFunctionTable s_table;

    [UnmanagedCallersOnly]
    public static void Bootstrap(NativeFunctionTable* table) {
        s_table = *table;
    }
}
