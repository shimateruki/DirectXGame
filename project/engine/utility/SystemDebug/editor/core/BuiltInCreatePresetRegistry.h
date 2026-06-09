#pragma once

class Object3dCommon;

class BuiltInCreatePresetRegistry {
public:
    static void EnsureRegistered(Object3dCommon* common);
};
