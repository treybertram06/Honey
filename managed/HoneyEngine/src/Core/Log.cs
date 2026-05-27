namespace HoneyEngine;

public static class Log {
    public static void Info(string msg)  => NativeBindings.Log_Info($"{msg}");
    public static void Warn(string msg)  => NativeBindings.Log_Warn($"{msg}");
    public static void Error(string msg) => NativeBindings.Log_Error($"{msg}");
}