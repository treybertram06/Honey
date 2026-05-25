using System.Runtime.InteropServices;

namespace HoneyEngine;

[StructLayout(LayoutKind.Sequential)]
public struct Vector4 {
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Vector4(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }
    public Vector4(Vector3 xyz, float w) : this(xyz.X, xyz.Y, xyz.Z, w) { }
    public Vector4(float value) : this(value, value, value, value) { }

    public static readonly Vector4 Zero = new(0, 0, 0, 0);
    public static readonly Vector4 One  = new(1, 1, 1, 1);

    public static Vector4 operator +(Vector4 a, Vector4 b) => new(a.X+b.X, a.Y+b.Y, a.Z+b.Z, a.W+b.W);
    public static Vector4 operator -(Vector4 a, Vector4 b) => new(a.X-b.X, a.Y-b.Y, a.Z-b.Z, a.W-b.W);
    public static Vector4 operator *(Vector4 v, float s)   => new(v.X*s, v.Y*s, v.Z*s, v.W*s);
    public static Vector4 operator *(float s,   Vector4 v) => v * s;

    public static bool operator ==(Vector4 a, Vector4 b) => a.X==b.X && a.Y==b.Y && a.Z==b.Z && a.W==b.W;
    public static bool operator !=(Vector4 a, Vector4 b) => !(a == b);
    public override bool Equals(object? obj) => obj is Vector4 v && this == v;
    public override int GetHashCode() => HashCode.Combine(X, Y, Z, W);

    // Convenience: pull out xyz as a Vector3
    public Vector3 XYZ => new(X, Y, Z);

    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}
