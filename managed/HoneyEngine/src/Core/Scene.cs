namespace HoneyEngine;

public static class Scene {

    // Returns an invalid Entity (ID == 0) if the prefab path was not found.
    public static Entity InstantiatePrefab(string path) {
        ulong id = NativeBindings.Scene_InstantiatePrefab(path);
        return new Entity(id);
    }
}