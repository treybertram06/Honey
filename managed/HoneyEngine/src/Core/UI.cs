namespace HoneyEngine;

// Where a panel's anchor point sits on the screen (and which point of the panel is pinned to it).
// Order must match the native GameUI::UIAnchor enum (row-major 3x3).
public enum Anchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
}

// Returned by UI.Panel. Disposing ends the panel, so `using` guarantees a matching EndPanel even on
// early return or exception. (A struct: no allocation.)
public readonly struct PanelScope : IDisposable {
    public void Dispose() => NativeBindings.UI_EndPanel();
}

// Immediate-mode screen-space UI. Call from EntityScript.OnGUI(); describe the UI every frame and
// read clicks back as return values. All sizes and offsets are in virtual pixels (reference height
// 1080) and scale with the window.
public static class UI {

    // Begins a panel pinned to `anchor`, moved by `offset` (virtual px). A 0 on either axis of `size`
    // auto-fits that axis. Panel ids are shared across scripts: reusing an id reuses the same window.
    public static PanelScope Panel(string id, Anchor anchor = Anchor.TopLeft, Vector2 offset = default,
                                   Vector2 size = default, bool background = false) {
        NativeBindings.UI_BeginPanel(id, (int)anchor, offset.X, offset.Y, size.X, size.Y, background);
        return new PanelScope();
    }

    // `size` is the text height in virtual px. Default colour is opaque white.
    public static void Label(string text, float size = 32f, Vector4? color = null) {
        Vector4 c = color ?? Vector4.One;
        NativeBindings.UI_Label(text, size, c.X, c.Y, c.Z, c.W);
    }

    // Returns true on the frame the button is clicked. `width` in virtual px; 0 fits the text.
    public static bool Button(string text, float width = 0f) => NativeBindings.UI_Button(text, width);

    // Vertical gap in virtual px.
    public static void Space(float height) => NativeBindings.UI_Spacing(height);

    // True when the mouse is over/captured by UI. Input.* polls raw hardware and ignores UI, so gameplay
    // that reacts to clicks should check this first: if (!UI.WantsMouse && Input.IsMouseButtonDown(...)) Shoot();
    public static bool WantsMouse => NativeBindings.UI_WantsMouse();
}
