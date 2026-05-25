namespace HoneyEngine;

// A lightweight handle into the C++ TransformComponent.
// Every get/set goes through a native call — there is no cached copy,
// so the values are always in sync with the engine.
public readonly struct Transform {
    private readonly ulong _entityId;

    internal Transform(ulong entityId) {
        _entityId = entityId;
    }

    public Vector3 Translation {
        get => NativeBindings.Entity_GetTranslation(_entityId);
        set => NativeBindings.Entity_SetTranslation(_entityId, value);
    }

    public Vector3 Rotation {
        get => NativeBindings.Entity_GetRotation(_entityId);
        set => NativeBindings.Entity_SetRotation(_entityId, value);
    }

    public Vector3 Scale {
        get => NativeBindings.Entity_GetScale(_entityId);
        set => NativeBindings.Entity_SetScale(_entityId, value);
    }
}