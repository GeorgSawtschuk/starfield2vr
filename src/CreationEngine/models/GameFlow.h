#pragma once
#include "glm/glm.hpp"

namespace GameFlow {
    enum class SeatedCameraMode : int
    {
        kStandard         = 0,
        kForceFirstPerson = 1,
        kHeadLocked        = 2,
    };

    struct MenuSettings
    {
        float hud_scale;
        int   perspective;
    };
    struct State
    {

        struct DebugWeaponData {
            int deepLevel;
            bool type;
            float yaw{0.f}, pitch{0.f}, roll {0.f};
        } debugWeaponData{};
        struct UiData {
            int rendered_menus_count[2]{};
            int pause_menu_flag[2]{};
            int modulino{0};
        } uiData{};

        struct RayCastData {
            glm::vec3 weaponRayCastOrigin{};
            glm::vec3 weaponRayCastDestination{};
        } rayCastData{};
    };

    extern State gState;

    void renderMenu(std::string_view menuNameHash);
    MenuSettings getMenuSettings(std::string_view menuNameHash);
    bool isShowingMenu();
    bool isShowingPauseMenu();
    bool isAimingDownSights();
    bool isWeaponDrawn();
    bool isImmovable();
    bool isControlledByAI();
    bool isInFirstPerson();
    bool isInSeatedThirdPerson();
    void resetGameState();
}

