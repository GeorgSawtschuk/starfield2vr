#include "GameFlow.h"
#include "ModSettingsStore.h"
#include "RE/P/PlayerCamera.h"
#include <CreationEngine/CreationEngineSingletonManager.h>
#include <mods/VR.hpp>

namespace GameFlow
{
    State gState{};
    auto vr = VR::get();

    const std::unordered_map<uint32_t, MenuSettings> menu_settings = {
            {         "Interface/ScopeMenu.swf"_DJB,   { 1.0f, 400 } },
            {         "Interface/ScopeMenu_LRG.swf"_DJB,   { 1.0f, 400 } },
            {       "Interface/MonocleMenu.swf"_DJB,   { 0.6f, 100 } },
            {       "Interface/MonocleMenu_LRG.swf"_DJB,   { 0.6f, 100 } }
    };

    void resetGameState() {
        gState.uiData.modulino++;
        gState.uiData.rendered_menus_count[gState.uiData.modulino % 2] = 0;
        gState.uiData.pause_menu_flag[gState.uiData.modulino % 2]      = 0;
    }

    void renderMenu(std::string_view menuNameHash) {
        switch (djb2Hash(menuNameHash.data())) {
        case "Interface/HUDMenu.gfx"_DJB:
        case "Interface/HUDMenu_LRG.gfx"_DJB:
        case "Interface/SpaceshipHudMenu.swf"_DJB:
        case "Interface/SpaceshipHudMenu_LRG.swf"_DJB:
            //            case "Interface/HUDMessagesMenu.gfx"_DJB:
        case "Interface/ScopeMenu.swf"_DJB:
        case "Interface/ScopeMenu_LRG.swf"_DJB:
        case "Interface/FavoritesMenu.swf"_DJB:
        case "Interface/FavoritesMenu_LRG.swf"_DJB:
        case "Interface/MonocleMenu.swf"_DJB:
        case "Interface/MonocleMenu_LRG.swf"_DJB:
        case "Interface/DialogueMenu.swf"_DJB:
        case "Interface/DialogueMenu_LRG.swf"_DJB:
            gState.uiData.rendered_menus_count[gState.uiData.modulino % 2]--;
            break;
        case "Interface/WorkshopMenu.swf"_DJB:
        case "Interface/WorkshopMenu_LRG.swf"_DJB:
        case "Interface/ResearchMenu.swf"_DJB:
        case "Interface/ResearchMenu_LRG.swf"_DJB:
        case "Interface/WeaponCraftingMenu.swf"_DJB:
        case "Interface/WeaponCraftingMenu_LRG.swf"_DJB:
        case "Interface/WeaponsCraftingMenu.swf"_DJB:
        case "Interface/WeaponsCraftingMenu_LRG.swf"_DJB:
        case "Interface/ArmorCraftingMenu.swf"_DJB:
        case "Interface/ArmorCraftingMenu_LRG.swf"_DJB:
        case "Interface/IndustrialCraftingMenu.swf"_DJB:
        case "Interface/IndustrialCraftingMenu_LRG.swf"_DJB:
        case "Interface/DrugsCraftingMenu.swf"_DJB:
        case "Interface/DrugsCraftingMenu.swf_LRG"_DJB:
        case "Interface/SpaceShipEditorMenu.swf"_DJB:
        case "Interface/SpaceShipEditorMenu_LRG.swf"_DJB:
        case "Interface/ShipCrewMenu.swf"_DJB:
        case "Interface/ShipCrewMenu_LRG.swf"_DJB:
        case "Interface/SkillsMenu.swf"_DJB:
        case "Interface/SkillsMenu_LRG.swf"_DJB:
        case "Interface/MainMenu.swf"_DJB:
        case "Interface/MainMenu_LRG.swf"_DJB:
        case "Interface/CursorMenu.swf"_DJB:
        case "Interface/CursorMenu_LRG.swf"_DJB:
        case "Interface/DataMenu.swf"_DJB:
        case "Interface/DataMenu_LRG.swf"_DJB:
        case "Interface/InventoryMenu.swf"_DJB:
        case "Interface/InventoryMenu_LRG.swf"_DJB:
        case "Interface/LoadingMenu.swf"_DJB:
        case "Interface/LoadingMenu_LRG.swf"_DJB:
        case "Interface/GalaxyStarMapMenu.swf"_DJB:
        case "Interface/GalaxyStarMapMenu_LRG.swf"_DJB:
        case "Interface/StarMapMenu.swf"_DJB:
        case "Interface/StarMapMenu_LRG.swf"_DJB:
            gState.uiData.rendered_menus_count[gState.uiData.modulino % 2]++;
            break;
        case "Interface/PauseMenu.swf"_DJB:
        case "Interface/PauseMenu_LRG.swf"_DJB:
            gState.uiData.rendered_menus_count[gState.uiData.modulino % 2]++;
            gState.uiData.pause_menu_flag[gState.uiData.modulino % 2] = 1;
            break;
        default:
            break;

        }
        // No shared-state writes beyond the double-buffered counter here: this runs concurrently on
        // Scaleform render worker threads while resetGameState()/on_draw_ui run on other threads. A
        // debug ui_parts vector previously push_back'd here raced their clear()/reads -> heap corruption.
    }

    MenuSettings getMenuSettings(std::string_view menuUrl) {
        auto hash = djb2Hash(menuUrl.data());
        MenuSettings settings = {gStore.hudSettings.hudScale, gStore.hudSettings.perspective };
        auto         it       = menu_settings.find(hash);
        if (it != menu_settings.end()) {
            settings.hud_scale = it->second.hud_scale;
            settings.perspective += it->second.perspective;
        }
        auto isMenu = gState.uiData.rendered_menus_count[(gState.uiData.modulino + 1) % 2] >= 0;
        if(isMenu) {
            settings.perspective = 0;
        }
        return settings;
    }

    bool isShowingMenu() {
        return gState.uiData.rendered_menus_count[(gState.uiData.modulino + 1) % 2] >= 0;
    }

    bool isShowingPauseMenu() {
        return gState.uiData.pause_menu_flag[(gState.uiData.modulino + 1) % 2] != 0;
    }

    bool isAimingDownSights() {
        auto p_player = CreationEngineSingletonManager::GetPlayerRef();
        return p_player && p_player->IsInIronSights();
    }
    bool isWeaponDrawn() {
        auto p_player = CreationEngineSingletonManager::GetPlayerRef();
        return p_player && p_player->IsWeaponDrawn();
    }
    bool isImmovable() {
        auto p_player = CreationEngineSingletonManager::GetPlayerRef();
        if(!p_player) {
            return true;
        }
        return p_player->Immovable();
    }
    bool isControlledByAI() {
        auto p_player = CreationEngineSingletonManager::GetPlayerRef();
        if(!p_player) {
            return false;
        }
        return p_player->ControlledByAI();
    }
    bool isInFirstPerson() {
        auto p_camera = CreationEngineSingletonManager::GetPlayerCameraSingleton();
        return p_camera && p_camera->IsInFirstPerson();
    }

    bool isInSeatedThirdPerson() {
        auto p_camera = CreationEngineSingletonManager::GetPlayerCameraSingleton();
        if (!p_camera || !p_camera->currentState) {
            return false;
        }
        // [SeatedCam] diagnostic logging confirmed sitting on a chair actually routes through the
        // field sdk-lite names pThirdPersonState (despite that field sitting at the enum slot for
        // kUnk06, not kThirdPerson -- see CreationEngineCameraManager.cpp's logCameraStateChangeIfNeeded).
        // pFurnitureCameraState never matched in that test. pFlightCameraState (ship pilot seat) is
        // still unverified -- kept as a candidate until confirmed in-game.
        return (uintptr_t) p_camera->currentState == (uintptr_t) p_camera->pThirdPersonState ||
               (uintptr_t) p_camera->currentState == (uintptr_t) p_camera->pFurnitureCameraState ||
               (uintptr_t) p_camera->currentState == (uintptr_t) p_camera->pFlightCameraState;
    }
}
