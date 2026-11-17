using System;

namespace HoneyEngine;

// A lightweight handle to an engine entity, identified by its UUID.
// readonly struct: stack-allocated, zero GC overhead, can't be mutated accidentally.
public readonly struct Entity : IEquatable<Entity> {
    internal readonly ulong ID;

    public Entity(ulong id) {
        ID = id;
    }

    // True only if the handle refers to an entity that currently exists in the active scene.
    // A stored Entity becomes invalid once its target is destroyed or the scene changes.
    public bool IsValid => ID != 0 && NativeBindings.Entity_IsValid(ID);

    // The entity's name (TagComponent). Returns "" for an invalid entity; setting is a no-op on one.
    public string Name {
        get => NativeBindings.Entity_GetName(ID);
        set => NativeBindings.Entity_SetName(ID, value);
    }

    public bool HasName(string name) => IsValid && Name == name;

    public Transform Transform => new Transform(ID);
    public Rigidbody Rigidbody => new Rigidbody(ID);

    public void Destroy() => NativeBindings.Entity_Destroy(ID);

    public bool Equals(Entity other) => ID == other.ID;
    public override bool Equals(object? obj) => obj is Entity e && Equals(e);
    public override int GetHashCode() => ID.GetHashCode();
    public static bool operator ==(Entity a, Entity b) => a.ID == b.ID;
    public static bool operator !=(Entity a, Entity b) => a.ID != b.ID;

    public override string ToString() => $"Entity({ID}, \"{Name}\")";
}
