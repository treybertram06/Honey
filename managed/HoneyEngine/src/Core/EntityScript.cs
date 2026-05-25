namespace HoneyEngine;

public abstract class EntityScript {
    // Set by ScriptRegistry before OnCreate is called.
    internal ulong EntityID { get; set; }

    public Entity Entity => new Entity(EntityID);

    public virtual void OnCreate()  {}
    public virtual void OnUpdate(float dt) {}
    public virtual void OnDestroy() {}
    public virtual void OnCollisionBegin(Entity other) {}
    public virtual void OnCollisionEnd(Entity other)   {}
}