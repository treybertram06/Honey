using System.Runtime.InteropServices;

namespace HoneyEngine;

[StructLayout(LayoutKind.Sequential)]
public struct Quaternion {
    public float X;
    public float Y;
    public float Z;
    public float W;

    public Quaternion(float x, float y, float z, float w) { X = x; Y = y; Z = z; W = w; }

    public static readonly Quaternion Identity = new(0, 0, 0, 1);

    public float Length() => MathF.Sqrt(X * X + Y * Y + Z * Z + W * W);

    public Quaternion Normalized() {
        float len = Length();
        return len > 0f ? new(X / len, Y / len, Z / len, W / len) : Identity;
    }

    // Hamilton product — combining two rotations: a then b
    public static Quaternion operator *(Quaternion a, Quaternion b) => new(
        a.W * b.X + a.X * b.W + a.Y * b.Z - a.Z * b.Y,
        a.W * b.Y - a.X * b.Z + a.Y * b.W + a.Z * b.X,
        a.W * b.Z + a.X * b.Y - a.Y * b.X + a.Z * b.W,
        a.W * b.W - a.X * b.X - a.Y * b.Y - a.Z * b.Z
    );

    public Vector3 Rotate(Vector3 v) {
        var q = new Quaternion(v.X, v.Y, v.Z, 0f);
        var conjugate = new Quaternion(-X, -Y, -Z, W);
        var result = this * q * conjugate;
        return new Vector3(result.X, result.Y, result.Z);
    }

    public static Quaternion FromEuler(float pitchRad, float yawRad, float rollRad) {
        float cy = MathF.Cos(rollRad  * 0.5f), sy = MathF.Sin(rollRad  * 0.5f);
        float cp = MathF.Cos(yawRad   * 0.5f), sp = MathF.Sin(yawRad   * 0.5f);
        float cr = MathF.Cos(pitchRad * 0.5f), sr = MathF.Sin(pitchRad * 0.5f);
        return new(
            sr * cp * cy - cr * sp * sy,
            cr * sp * cy + sr * cp * sy,
            cr * cp * sy - sr * sp * cy,
            cr * cp * cy + sr * sp * sy
        );
    }

    public override string ToString() => $"({X}, {Y}, {Z}, {W})";
}