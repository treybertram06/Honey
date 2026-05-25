namespace HoneyEngine;

public static class Log {
    public static void Info(string msg)  => NativeBindings.Log_Info($"[C#] {msg}");
    public static void Warn(string msg)  => NativeBindings.Log_Warn($"[C#] {msg}");
    public static void Error(string msg) => NativeBindings.Log_Error($"[C#] {msg}");
}