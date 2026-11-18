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

// Horizontal text alignment within a panel's content width.
public enum TextAlign { Left, Center, Right }

// A loaded font. Default (handle 0) is the engine's built-in UI font.
public readonly struct UIFont {
    internal readonly int Handle;
    internal UIFont(int handle) { Handle = handle; }
}

// Visual style for all UI. Create one, tweak it, then assign it: `UI.Theme = theme;`.
// Sizes are virtual px. Button colours are derived from Accent (idle: faint white, hover: accent glow,
// pressed: strong accent). Defaults are a minimal translucent blue look.
public sealed class UITheme {
    public Vector4 PanelColor = new(0.05f, 0.07f, 0.11f, 0.72f);
    public Vector4 TextColor = new(0.93f, 0.95f, 1.00f, 1.00f);
    public Vector4 Accent = new(0.25f, 0.55f, 1.00f, 1.00f);
    public float Rounding = 10f;
    public float PanelPadding = 20f;
    public float ItemSpacing = 12f;
    public float ButtonFontSize = 28f;
    public Vector2 ButtonPadding = new(28f, 14f);
    public UIFont Font;
}

// Immediate-mode screen-space UI. Call from EntityScript.OnGUI(); describe the UI every frame and
// read clicks back as return values. All sizes and offsets are in virtual pixels (reference height
// 1080) and scale with the window.
public static unsafe class UI {

    private static UITheme s_theme = new();

    // The current theme. Assigning applies it immediately and it persists across scenes. Edit a theme
    // object's fields freely, but assign it again for the change to take effect.
    public static UITheme Theme {
        get => s_theme;
        set {
            s_theme = value ?? new UITheme();
            ApplyTheme(s_theme);
        }
    }

    // Loads a .ttf (asset-relative path like "fonts/Inter/Inter-Regular.ttf", or absolute). Safe to call
    // every frame (cached by path), but calling once in OnCreate is cleaner. A bad path logs a warning
    // and returns the default font.
    public static UIFont LoadFont(string path) => new(NativeBindings.UI_LoadFont(path));

    private static void ApplyTheme(UITheme t) {
        // Layout must match glue_ui_set_theme in csharp_script_glue.cpp.
        float* v = stackalloc float[18];
        v[0] = t.PanelColor.X; v[1] = t.PanelColor.Y; v[2] = t.PanelColor.Z; v[3] = t.PanelColor.W;
        v[4] = t.TextColor.X;  v[5] = t.TextColor.Y;  v[6] = t.TextColor.Z;  v[7] = t.TextColor.W;
        v[8] = t.Accent.X;     v[9] = t.Accent.Y;     v[10] = t.Accent.Z;    v[11] = t.Accent.W;
        v[12] = t.Rounding; v[13] = t.PanelPadding; v[14] = t.ItemSpacing;
        v[15] = t.ButtonFontSize; v[16] = t.ButtonPadding.X; v[17] = t.ButtonPadding.Y;
        NativeBindings.UI_SetTheme(v, 18, t.Font.Handle);
    }

    // Begins a panel pinned to `anchor`, moved by `offset` (virtual px). A 0 on either axis of `size`
    // auto-fits that axis. Panel ids are shared across scripts: reusing an id reuses the same window.
    public static PanelScope Panel(string id, Anchor anchor = Anchor.TopLeft, Vector2 offset = default,
                                   Vector2 size = default, bool background = false) {
        NativeBindings.UI_BeginPanel(id, (int)anchor, offset.X, offset.Y, size.X, size.Y, background);
        return new PanelScope();
    }

    // `size` is the text height in virtual px. Colour defaults to the theme's TextColor; font defaults
    // to the theme's Font when left as `default`.
    public static void Label(string text, float size = 32f, Vector4? color = null, UIFont font = default,
                             TextAlign align = TextAlign.Left) {
        Vector4 c = color ?? s_theme.TextColor;
        NativeBindings.UI_Label(text, size, c.X, c.Y, c.Z, c.W, font.Handle != 0 ? font.Handle : s_theme.Font.Handle, (int)align);
    }

    // Returns true on the frame the button is clicked. `width` in virtual px; 0 fits the text.
    public static bool Button(string text, float width = 0f) => NativeBindings.UI_Button(text, width);

    // Vertical gap in virtual px.
    public static void Space(float height) => NativeBindings.UI_Spacing(height);

    // True when the mouse is over/captured by UI. Input.* polls raw hardware and ignores UI, so gameplay
    // that reacts to clicks should check this first: if (!UI.WantsMouse && Input.IsMouseButtonDown(...)) Shoot();
    public static bool WantsMouse => NativeBindings.UI_WantsMouse();
}
