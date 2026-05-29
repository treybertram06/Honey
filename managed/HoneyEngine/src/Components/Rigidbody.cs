namespace HoneyEngine;

public readonly struct Rigidbody {
    private readonly ulong _entityId;

    internal Rigidbody(ulong entityId) {
        _entityId = entityId;
    }

    public Vector3 Velocity {
        get => NativeBindings.Rigidbody_GetVelocity(_entityId);
        set => NativeBindings.Rigidbody_SetVelocity(_entityId, value);
    }

    public void ApplyForce(Vector3 force) =>
        NativeBindings.Rigidbody_ApplyForce(_entityId, force.X, force.Y, force.Z);

    public void ApplyImpulse(Vector3 impulse) =>
        NativeBindings.Rigidbody_ApplyImpulse(_entityId, impulse.X, impulse.Y, impulse.Z);

    public void SetPosition(Vector3 position) =>
        NativeBindings.Rigidbody_SetPosition(_entityId, position);
}