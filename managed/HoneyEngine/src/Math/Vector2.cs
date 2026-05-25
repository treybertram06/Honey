using System.Runtime.InteropServices;

namespace HoneyEngine;

[StructLayout(LayoutKind.Sequential)]
public struct Vector2 {
    public float X;
    public float Y;

    public Vector2(float x, float y) { X = x; Y = y; }
    public Vector2(float value) : this(value, value) { }

    public static readonly Vector2 Zero  = new(0, 0);
    public static readonly Vector2 One   = new(1, 1);
    public static readonly Vector2 Up    = new(0, 1);
    public static readonly Vector2 Right = new(1, 0);

    public static Vector2 operator +(Vector2 a, Vector2 b) => new(a.X + b.X, a.Y + b.Y);
    public static Vector2 operator -(Vector2 a, Vector2 b) => new(a.X - b.X, a.Y - b.Y);
    public static Vector2 operator *(Vector2 v, float s)   => new(v.X * s,   v.Y * s);
    public static Vector2 operator *(float s,   Vector2 v) => v * s;
    public static Vector2 operator /(Vector2 v, float s)   => new(v.X / s,   v.Y / s);
    public static Vector2 operator -(Vector2 v)            => new(-v.X, -v.Y);

    public static bool operator ==(Vector2 a, Vector2 b) => a.X == b.X && a.Y == b.Y;
    public static bool operator !=(Vector2 a, Vector2 b) => !(a == b);
    public override bool Equals(object? obj) => obj is Vector2 v && this == v;
    public override int GetHashCode() => HashCode.Combine(X, Y);

    public float Length() => MathF.Sqrt(X * X + Y * Y);
    public Vector2 Normalized() {
        float l = Length(); return l > 0f ? this / l : Zero;
    }

    public override string ToString() => $"({X}, {Y})";
}
