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

    internal static bool Entity_IsValid(ulong id) => InternalCalls.s_table.Entity_IsValid(id) != 0;

    // Native returns the full UTF-8 length; if it doesn't fit the stack buffer, retry with a heap buffer.
    internal static string Entity_GetName(ulong id) {
        byte* stack = stackalloc byte[256];
        int len = InternalCalls.s_table.Entity_GetName(id, stack, 256);
        if (len <= 0) return string.Empty;
        if (len < 256) return Encoding.UTF8.GetString(stack, len);

        byte[] heap = new byte[len + 1];
        fixed (byte* p = heap) {
            InternalCalls.s_table.Entity_GetName(id, p, heap.Length);
        }
        return Encoding.UTF8.GetString(heap, 0, len);
    }

    internal static void Entity_SetName(ulong id, string name) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(name, stack, k_utf8_stack_size);
        try { InternalCalls.s_table.Entity_SetName(id, buf); }
        finally { FreeUtf8(buf, stack); }
    }

    // --- UTF-8 string marshalling ---
    // stackalloc memory dies with the method that allocated it, so each wrapper owns its stack buffer
    // and passes it in; the helper only decides whether it is big enough. Pair every EncodeUtf8 with
    // FreeUtf8 (in a finally). The returned pointer is NUL-terminated and valid only for the native call.
    private const int k_utf8_stack_size = 512;

    private static byte* EncodeUtf8(string s, byte* stack, int stack_size) {
        s ??= string.Empty;
        int count = Encoding.UTF8.GetByteCount(s);
        byte* dst = stack;
        if (count + 1 > stack_size)
            dst = (byte*)Marshal.AllocHGlobal(count + 1);   // rare: oversized string
        Encoding.UTF8.GetBytes(s, new Span<byte>(dst, count));
        dst[count] = 0;
        return dst;
    }

    private static void FreeUtf8(byte* buf, byte* stack) {
        if (buf != stack) Marshal.FreeHGlobal((nint)buf);
    }

    // --- Scene ---
    internal static ulong Scene_InstantiatePrefab(string path) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(path, stack, k_utf8_stack_size);
        try { return InternalCalls.s_table.Scene_InstantiatePrefab(buf); }
        finally { FreeUtf8(buf, stack); }
    }

    internal static void Scene_Load(string portable_path) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(portable_path, stack, k_utf8_stack_size);
        try { InternalCalls.s_table.Scene_Load(buf); }
        finally { FreeUtf8(buf, stack); }
    }

    // --- Physics ---

    internal static void Rigidbody2D_ApplyLinearImpulse(ulong id, float x, float y, float wake) {
        InternalCalls.s_table.Rigidbody2D_ApplyLinearImpulse(id, x, y, wake);
    }

    internal static void Rigidbody_ApplyForce(ulong id, float x, float y, float z) {
        InternalCalls.s_table.Rigidbody_ApplyForce(id, x, y, z);
    }

    internal static void Rigidbody_ApplyImpulse(ulong id, float x, float y, float z) {
        InternalCalls.s_table.Rigidbody_ApplyImpulse(id, x, y, z);
    }

    internal static Vector3 Rigidbody_GetVelocity(ulong id) {
        float* xyz = stackalloc float[3];
        InternalCalls.s_table.Rigidbody_GetVelocity(id, xyz);
        return new Vector3(xyz[0], xyz[1], xyz[2]);
    }

    internal static void Rigidbody_SetVelocity(ulong id, Vector3 v) {
        InternalCalls.s_table.Rigidbody_SetVelocity(id, v.X, v.Y, v.Z);
    }

    internal static void Rigidbody_SetPosition(ulong id, Vector3 v) {
        InternalCalls.s_table.Rigidbody_SetPosition(id, v.X, v.Y, v.Z);
    }

    // --- Logging ---
    // Strings must be encoded as UTF-8 bytes before crossing the native boundary.
    // stackalloc avoids heap allocation for something called as often as logging.

    internal static void Log_Info(string msg)  => LogNative(InternalCalls.s_table.Log_Info,  msg);
    internal static void Log_Warn(string msg)  => LogNative(InternalCalls.s_table.Log_Warn,  msg);
    internal static void Log_Error(string msg) => LogNative(InternalCalls.s_table.Log_Error, msg);

    private static void LogNative(delegate* unmanaged<byte*, void> fn, string msg) {
        // 1024 bytes covers almost all log messages; longer ones fall back to the heap inside EncodeUtf8.
        byte* stack = stackalloc byte[1024];
        byte* buf = EncodeUtf8(msg, stack, 1024);
        try { fn(buf); }
        finally { FreeUtf8(buf, stack); }
    }

    // --- Entity lifecycle ---

    internal static void Entity_Destroy(ulong id) {
        InternalCalls.s_table.Entity_Destroy(id);
    }

    // --- Scene queries ---

    internal static ulong Scene_FindEntityByName(string name) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(name, stack, k_utf8_stack_size);
        try { return InternalCalls.s_table.Scene_FindEntityByName(buf); }
        finally { FreeUtf8(buf, stack); }
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

    internal static float Input_GetMouseDeltaX() => InternalCalls.s_table.Input_GetMouseDeltaX();
    internal static float Input_GetMouseDeltaY() => InternalCalls.s_table.Input_GetMouseDeltaY();

    internal static void Cursor_SetLocked(bool locked) => InternalCalls.s_table.Cursor_SetLocked((byte)(locked ? 1 : 0));
    internal static void Application_Quit() => InternalCalls.s_table.Application_Quit();
    internal static float Time_GetDeltaTime() => InternalCalls.s_table.Time_GetDeltaTime();
    internal static float Time_GetElapsed() => InternalCalls.s_table.Time_GetElapsed();

    // --- UI ---
    internal static void UI_BeginPanel(string id, int anchor, float offset_x, float offset_y,
                                       float size_w, float size_h, bool background) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(id, stack, k_utf8_stack_size);
        try {
            InternalCalls.s_table.UI_BeginPanel(buf, anchor, offset_x, offset_y, size_w, size_h,
                                                (byte)(background ? 1 : 0));
        }
        finally { FreeUtf8(buf, stack); }
    }

    internal static void UI_EndPanel() => InternalCalls.s_table.UI_EndPanel();

    internal static void UI_Label(string text, float size, float r, float g, float b, float a, int font, int align) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(text, stack, k_utf8_stack_size);
        try { InternalCalls.s_table.UI_Label(buf, size, r, g, b, a, font, align); }
        finally { FreeUtf8(buf, stack); }
    }

    internal static bool UI_Button(string text, float width) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(text, stack, k_utf8_stack_size);
        try { return InternalCalls.s_table.UI_Button(buf, width) != 0; }
        finally { FreeUtf8(buf, stack); }
    }

    internal static void UI_Spacing(float h) => InternalCalls.s_table.UI_Spacing(h);
    internal static bool UI_WantsMouse() => InternalCalls.s_table.UI_WantsMouse() != 0;

    internal static void UI_SetTheme(float* values, int count, int font) => InternalCalls.s_table.UI_SetTheme(values, count, font);

    internal static int UI_LoadFont(string path) {
        byte* stack = stackalloc byte[k_utf8_stack_size];
        byte* buf = EncodeUtf8(path, stack, k_utf8_stack_size);
        try { return InternalCalls.s_table.UI_LoadFont(buf); }
        finally { FreeUtf8(buf, stack); }
    }
}
