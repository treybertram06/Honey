using System.Runtime.InteropServices;

namespace HoneyEngine;

[StructLayout(LayoutKind.Sequential)] // Requires that struct fields are laid out in memory exactly as they are written here, no reordering
public struct Vector3 {
    public float X;
    public float Y;
    public float Z;

    public Vector3(float x, float y, float z) {
        X = x; Y = y; Z = z;
    }

    public Vector3(float value) : this (value, value, value) { } // Single value constructor
    public static readonly Vector3 Zero    = new(0,  0,  0);
    public static readonly Vector3 One     = new(1,  1,  1);
    public static readonly Vector3 Up      = new(0,  1,  0);
    public static readonly Vector3 Down    = new(0, -1,  0);
    public static readonly Vector3 Right   = new(1,  0,  0);
    public static readonly Vector3 Left    = new(-1, 0,  0);
    public static readonly Vector3 Forward = new(0,  0, -1);
    public static readonly Vector3 Back    = new(0,  0,  1);

    // Arithmetic operators — let users write: pos + velocity * dt
    public static Vector3 operator +(Vector3 a, Vector3 b) => new(a.X + b.X, a.Y + b.Y, a.Z + b.Z);
    public static Vector3 operator -(Vector3 a, Vector3 b) => new(a.X - b.X, a.Y - b.Y, a.Z - b.Z);
    public static Vector3 operator *(Vector3 v, float s)   => new(v.X * s,   v.Y * s,   v.Z * s);
    public static Vector3 operator *(float s,   Vector3 v) => v * s;
    public static Vector3 operator /(Vector3 v, float s)   => new(v.X / s,   v.Y / s,   v.Z / s);
    public static Vector3 operator -(Vector3 v)            => new(-v.X, -v.Y, -v.Z);

    public static bool operator ==(Vector3 a, Vector3 b) => a.X == b.X && a.Y == b.Y && a.Z == b.Z;
    public static bool operator !=(Vector3 a, Vector3 b) => !(a == b);

      // C# requires these when you override ==
    public override bool Equals(object? obj) => obj is Vector3 v && this == v;
    public override int GetHashCode() => HashCode.Combine(X, Y, Z);

      public float Length() => MathF.Sqrt(X * X + Y * Y + Z * Z);

    public Vector3 Normalized() {
        float len = Length();
        return len > 0f ? this / len : Zero;
    }

    public static float Dot(Vector3 a, Vector3 b) => a.X * b.X + a.Y * b.Y + a.Z * b.Z;

    public static Vector3 Cross(Vector3 a, Vector3 b) => new(
        a.Y * b.Z - a.Z * b.Y,
        a.Z * b.X - a.X * b.Z,
        a.X * b.Y - a.Y * b.X
      );

      public override string ToString() => $"({X}, {Y}, {Z})";
}
