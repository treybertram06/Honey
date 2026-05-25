namespace HoneyEngine;

// A lightweight handle to an engine entity, identified by its UUID.
// readonly struct: stack-allocated, zero GC overhead, can't be mutated accidentally.
public readonly struct Entity {
    internal readonly ulong ID;

    public Entity(ulong id) {
        ID = id;
    }

    public Transform Transform => new Transform(ID);
}