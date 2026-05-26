using System.Runtime.InteropServices;
using System.Text;

namespace HoneyEngine;

// Typed wrappers over the raw function pointer table in InternalCalls.
// Everything above this layer is pure safe C# — all unsafe code lives here.
internal static unsafe class NativeBindings {

    // --- Transform ---

    internal static Vector3 Entity_GetTranslation(ulong id) {
        float* xyz = stackalloc float[3];
        InternalCalls.s_table.Entity_GetTranslation(id, xyz);
        return new Vector3(xyz[0], xyz[1], xyz[2]);
    }

    internal static void Entity_SetTranslation(ulong id, Vector3 v) {
        float* xyz = stackalloc float[3];
        xyz[0] = v.X; xyz[1] = v.Y; xyz[2] = v.Z;
        InternalCalls.s_table.Entity_SetTranslation(id, xyz);
    }

    internal static Vector3 Entity_GetRotation(ulong id) {
        float* xyz = stackalloc float[3];
        InternalCalls.s_table.Entity_GetRotation(id, xyz);
        return new Vector3(xyz[0], xyz[1], xyz[2]);
    }

    internal static void Entity_SetRotation(ulong id, Vector3 v) {
        float* xyz = stackalloc float[3];
        xyz[0] = v.X; xyz[1] = v.Y; xyz[2] = v.Z;
        InternalCalls.s_table.Entity_SetRotation(id, xyz);
    }

    internal static Vector3 Entity_GetScale(ulong id) {
        float* xyz = stackalloc float[3];
        InternalCalls.s_table.Entity_GetScale(id, xyz);
        return new Vector3(xyz[0], xyz[1], xyz[2]);
    }

    internal static void Entity_SetScale(ulong id, Vector3 v) {
        float* xyz = stackalloc float[3];
        xyz[0] = v.X; xyz[1] = v.Y; xyz[2] = v.Z;
        InternalCalls.s_table.Entity_SetScale(id, xyz);
    }

    // --- Scene ---
    internal static ulong Scene_InstantiatePrefab(string path) {
        // Encode the path as a null-terminated UTF-8 byte buffer.
        int maxBytes = Encoding.UTF8.GetMaxByteCount(path.Length) + 1;
        if (maxBytes <= 512) {
            byte* buf = stackalloc byte[512];
            int written = Encoding.UTF8.GetBytes(path, new Span<byte>(buf, 511));
            buf[written] = 0;
            return InternalCalls.s_table.Scene_InstantiatePrefab(buf);
        } else {
            // Fallback for unusually long paths
            byte[] bytes = Encoding.UTF8.GetBytes(path);
            fixed (byte* p = bytes) {
                byte* buf = (byte*)Marshal.AllocHGlobal(bytes.Length + 1);
                Buffer.MemoryCopy(p, buf, bytes.Length + 1, bytes.Length);
                buf[bytes.Length] = 0;
                ulong result = InternalCalls.s_table.Scene_InstantiatePrefab(buf);
                Marshal.FreeHGlobal((nint)buf);
                return result;
            }
        }
    }

    // --- Physics ---

    internal static void Rigidbody2D_ApplyLinearImpulse(ulong id, float x, float y, float wake) {
        InternalCalls.s_table.Rigidbody2D_ApplyLinearImpulse(id, x, y, wake);
    }

    // --- Logging ---
    // Strings must be encoded as UTF-8 bytes before crossing the native boundary.
    // stackalloc avoids heap allocation for something called as often as logging.

    internal static void Log_Info(string msg)  => LogNative(InternalCalls.s_table.Log_Info,  msg);
    internal static void Log_Warn(string msg)  => LogNative(InternalCalls.s_table.Log_Warn,  msg);
    internal static void Log_Error(string msg) => LogNative(InternalCalls.s_table.Log_Error, msg);

    private static void LogNative(delegate* unmanaged<byte*, void> fn, string msg) {
        // Encode to a stack buffer. 1024 bytes covers almost all log messages.
        // For longer strings we fall back to a heap allocation via Marshal.
        int maxBytes = Encoding.UTF8.GetMaxByteCount(msg.Length) + 1; // +1 for null terminator
        if (maxBytes <= 1024) {
            byte* buf = stackalloc byte[1024];
            int written = Encoding.UTF8.GetBytes(msg, new Span<byte>(buf, 1023));
            buf[written] = 0; // null terminate
            fn(buf);
        } else {
            // Fallback: heap alloc for unusually long strings
            byte[] bytes = Encoding.UTF8.GetBytes(msg);
            fixed (byte* p = bytes) {
                // Copy into a null-terminated buffer
                byte* buf = (byte*)Marshal.AllocHGlobal(bytes.Length + 1);
                Buffer.MemoryCopy(p, buf, bytes.Length + 1, bytes.Length);
                buf[bytes.Length] = 0;
                fn(buf);
                Marshal.FreeHGlobal((nint)buf);
            }
        }
    }

    // --- Input ---

    internal static bool Input_IsKeyDown(int keyCode) {
        return InternalCalls.s_table.Input_IsKeyDown(keyCode) != 0;
    }

    internal static bool Input_IsMouseButtonDown(int button) {
        return InternalCalls.s_table.Input_IsMouseButtonDown(button) != 0;
    }

    internal static float Input_GetMouseX() {
        return InternalCalls.s_table.Input_GetMouseX();
    }

    internal static float Input_GetMouseY() {
        return InternalCalls.s_table.Input_GetMouseY();
    }

    internal static bool Input_IsMouseCaptured() {
        return InternalCalls.s_table.Input_IsMouseCaptured() != 0;
    }
}