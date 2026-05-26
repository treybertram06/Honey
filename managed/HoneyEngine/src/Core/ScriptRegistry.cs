using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace HoneyEngine;

public static unsafe class ScriptRegistry {

    // Maps class name (e.g. "PlayerController") → its Type, for fast instance creation.
    private static readonly Dictionary<string, Type> s_types = new();

    // Weak reference so the ALC can be garbage-collected after Unload() is called.
    // We don't want to keep the old assembly alive just because the registry saw it.
    private static WeakReference<AssemblyLoadContext>? s_scriptAlc;

    // Called by C++ at startup or on hot reload to load the user script DLL.
    // Creates a collectible AssemblyLoadContext so the assembly can be unloaded later.
    private sealed class ScriptAlc : AssemblyLoadContext {
        private static readonly Assembly s_honeyEngine = typeof(EntityScript).Assembly;
        public ScriptAlc() : base("UserScripts", isCollectible: true) { }
        protected override Assembly? Load(AssemblyName name) =>
            name.Name == s_honeyEngine.GetName().Name ? s_honeyEngine : null;
    }

    [UnmanagedCallersOnly]
    public static void RegisterAssembly(char* pathPtr) {
        string path = Marshal.PtrToStringAnsi((nint)pathPtr)!;

        var alc = new ScriptAlc();
        s_scriptAlc = new WeakReference<AssemblyLoadContext>(alc);

        Assembly asm;
        try {
            asm = alc.LoadFromAssemblyPath(path);
        } catch (Exception ex) {
            Log.Error($"ScriptRegistry: failed to load assembly '{path}': {ex.Message}");
            return;
        }

        s_types.Clear();
        foreach (var type in asm.GetTypes()) {
            if (!type.IsAbstract && type.IsSubclassOf(typeof(EntityScript)))
                s_types[type.Name] = type;
        }

        Log.Info($"ScriptRegistry: loaded {s_types.Count} script(s) from '{path}'");
    }

    // Disposes the collectible ALC. Clear type references first so the GC can collect
    // the ALC promptly; then force a collection so the old DLL file is released before
    // the next RegisterAssembly call maps the new one.
    [UnmanagedCallersOnly]
    public static void UnloadScriptAssembly() {
        s_types.Clear();
        if (s_scriptAlc?.TryGetTarget(out var alc) == true) {
            alc.Unload();
            s_scriptAlc = null;
        }
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
    }

    // Creates a script instance by class name and returns a GCHandle as an IntPtr.
    // C++ stores this IntPtr as a void* and passes it back on every subsequent call.
    // GCHandle.Alloc pins the object so the GC won't move or collect it.
    [UnmanagedCallersOnly]
    public static nint CreateInstance(char* classNamePtr) {
        string className = Marshal.PtrToStringAnsi((nint)classNamePtr)!;

        if (!s_types.TryGetValue(className, out var type)) {
            Log.Error($"ScriptRegistry: unknown script class '{className}'");
            return 0;
        }

        try {
            var instance = Activator.CreateInstance(type)!;
            return GCHandle.ToIntPtr(GCHandle.Alloc(instance));
        } catch (Exception ex) {
            Log.Error($"ScriptRegistry: failed to create '{className}': {ex.Message}");
            return 0;
        }
    }

    // Releases the GCHandle, allowing the GC to collect the script instance.
    [UnmanagedCallersOnly]
    public static void DestroyInstance(nint handle) {
        if (handle != 0)
            GCHandle.FromIntPtr(handle).Free();
    }

    // --- Lifecycle call-throughs ---
    // C++ passes back the GCHandle it received from CreateInstance.
    // We unwrap it, set EntityID, then call the virtual method.

    [UnmanagedCallersOnly]
    public static void CallOnCreate(nint handle, ulong entityId) {
        var script = Unwrap(handle);
        script.EntityID = entityId;
        script.OnCreate();
    }

    [UnmanagedCallersOnly]
    public static void CallOnUpdate(nint handle, ulong entityId, float dt) {
        var script = Unwrap(handle);
        script.OnUpdate(dt);
    }

    [UnmanagedCallersOnly]
    public static void CallOnDestroy(nint handle, ulong entityId) {
        var script = Unwrap(handle);
        script.OnDestroy();
    }

    [UnmanagedCallersOnly]
    public static void CallCollisionBegin(nint handle, ulong entityId, ulong otherId) {
        var script = Unwrap(handle);
        script.OnCollisionBegin(new Entity(otherId));
    }

    [UnmanagedCallersOnly]
    public static void CallCollisionEnd(nint handle, ulong entityId, ulong otherId) {
        var script = Unwrap(handle);
        script.OnCollisionEnd(new Entity(otherId));
    }

    [UnmanagedCallersOnly]
    public static byte ClassExists(char* classNamePtr) {
        string className = Marshal.PtrToStringAnsi((nint)classNamePtr)!;
        return s_types.ContainsKey(className) ? (byte)1 : (byte)0;
    }

    private static EntityScript Unwrap(nint handle) =>
        (EntityScript)GCHandle.FromIntPtr(handle).Target!;
}