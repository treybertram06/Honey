namespace HoneyEngine;

public static class Scene {

    // Returns an invalid Entity (ID == 0) if the prefab path was not found.
    public static Entity InstantiatePrefab(string path) {
        ulong id = NativeBindings.Scene_InstantiatePrefab(path);
        return new Entity(id);
    }

    // Returns an invalid Entity (ID == 0) if no entity with that name exists.
    public static Entity FindEntityByName(string name) {
        ulong id = NativeBindings.Scene_FindEntityByName(name);
        return new Entity(id);
    }
}