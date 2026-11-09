namespace HoneyEngine;

public static class Time
{
    public static float DeltaTime => NativeBindings.Time_GetDeltaTime();
    public static float Elapsed   => NativeBindings.Time_GetElapsed();
}