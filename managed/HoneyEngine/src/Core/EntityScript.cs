namespace HoneyEngine;

public abstract class EntityScript {
    // Set by ScriptRegistry before OnCreate is called.
    internal ulong EntityID { get; set; }

    public Entity Entity => new Entity(EntityID);

    public virtual void OnCreate()  {}
    public virtual void OnUpdate(float dt) {}
    public virtual void OnDestroy() {}
    // Called once per frame (immediate-mode UI). Describe the UI with the UI class; see UI.cs.
    public virtual void OnGUI() {}
    public virtual void OnCollisionBegin(Entity other) {}
    public virtual void OnCollisionEnd(Entity other)   {}
}