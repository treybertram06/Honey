namespace HoneyEngine;

public static class Cursor
{
    public static bool Locked  { get => NativeBindings.Input_IsMouseCaptured(); set => NativeBindings.Cursor_SetLocked(value); }
    public static bool Visible { get => !Locked; set => Locked = !value; }
}