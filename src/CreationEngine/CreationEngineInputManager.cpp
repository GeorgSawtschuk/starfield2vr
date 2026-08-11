//
// Created by sergp on 6/13/2024.
//
#include <memory>
#include <cstdint>

#include "CreationEngineInputManager.h"
#include <CreationEngine/memory/offsets.h>
#include <CreationEngine/models/ModSettingsStore.h>
#include <REL/Relocation.h>
#include <mods/VR.hpp>

CreationEngineInputManager::~CreationEngineInputManager() {
    vigem_target_remove(client, pad);
    vigem_target_free(pad);
}

void CreationEngineInputManager::Reset() {
    if(!connected) {
        return;
    }
    vigem_target_remove(client, pad);
    vigem_target_free(pad);
    connected = false;
    Init();
}

void CreationEngineInputManager::Init() {
    if(connected || client == nullptr) {
        return;
    }
    const auto retval = vigem_connect(client);

    if (!VIGEM_SUCCESS(retval))
    {
        spdlog::error("ViGEm Bus connection failed with error code: 0x %d", retval);
        return;
    }
    //
    // Add client to the bus, this equals a plug-in event
    //
    pad = vigem_target_x360_alloc();

    const auto pir = vigem_target_add(client, pad);

    if (!VIGEM_SUCCESS(pir))
    {
        spdlog::error("Target plugin failed with error code: 0x %d", pir);
    }
    spdlog::info("ViGEm Bus connection successful!");
    connected = true;

}

bool CreationEngineInputManager::Hook() {
    spdlog::info("Entering CreationEngineInputManager::Hook().");
    REL::Relocation<uintptr_t> gamepadDevicePollFuncAddr{ GameStore::MemoryOffsets::Gamepad::PollVfunc() };
    m_poll_events_hook = std::make_unique<FunctionHook>(gamepadDevicePollFuncAddr.address(), reinterpret_cast<uintptr_t>(&CreationEngineInputManager::onPollGamepadState));
    if(!m_poll_events_hook->create()) {
        spdlog::error("Failed to hook gamepadDevicePoll");
        return false;
    }
    return true;
}

CreationEngineInputManager::CreationEngineInputManager(): connected(false), client(vigem_alloc()), pad(nullptr)
{
    if (client == nullptr) {
        spdlog::error("Uh, not enough memory to do that?!");
        return;
    }
    Hook();
}

void CreationEngineInputManager::onPollGamepadState(__int64 a1, double xmm2) {
    static auto instance = CreationEngineInputManager::Get();
    instance->UpdateDeviceState();
    static auto original_fn = instance->m_poll_events_hook->get_original<decltype(CreationEngineInputManager::onPollGamepadState)>();
    return original_fn(a1, xmm2);
}

void CreationEngineInputManager::UpdateDeviceState() {
    if(!connected) {
        return;
    }
    static auto vr = VR::get();

    if (!vr->is_hmd_active()) {
        return;
    }

    // The engine polls the gamepad device several times per rendered frame, and far more
    // aggressively while an input-capture screen is open (e.g. key rebinding in Settings, which
    // busy-polls waiting for a press). update_action_states() issues an OpenXR xrSyncActions and
    // vigem_target_x360_update() a synchronous driver IOCTL; both are meant to run once per frame.
    // Running them per poll lets the menu's high-frequency polling throttle the render loop to
    // ~10 fps. Limit the expensive sync to the first poll of each rendered frame — with a time
    // fallback so input keeps flowing when the render frame counter stalls (loading screens, hangs).
    static int      lastSyncedFrame  = -1;
    static uint32_t pollsThisFrame   = 0;
    static uint32_t maxPollsPerFrame = 0;
    static int64_t  lastSyncTick     = 0;
    LARGE_INTEGER   now, tickFreq;
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&tickFreq);
    auto fc = GameFlow::renderLoopFrameCount();
    if (fc == lastSyncedFrame && (now.QuadPart - lastSyncTick) * 1000 < 10 * tickFreq.QuadPart) {
        ++pollsThisFrame;
        return;
    }
    lastSyncTick = now.QuadPart;
    if (pollsThisFrame > maxPollsPerFrame) {
        maxPollsPerFrame = pollsThisFrame;
        spdlog::info("[INPUT] gamepad polls/frame peaked at {} (redundant OpenXR/ViGEm syncs now skipped)", pollsThisFrame);
    }
    lastSyncedFrame = fc;
    pollsThisFrame  = 1;

    XUSB_REPORT report = {0};
    vr->update_action_states();

    uint32_t retval;
    vr->on_xinput_get_state(&retval, 0, (XINPUT_STATE*)&report);
    vigem_target_x360_update(client, pad, report);
}
