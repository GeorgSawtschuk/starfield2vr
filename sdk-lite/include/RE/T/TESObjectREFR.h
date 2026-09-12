#pragma once
#include "RE/N/NiAVObject.h"
#include "RE/N/NiPoint3.h"
namespace RE
{
  class BGSInventoryList;
  class TESObjectCELL;
	struct OBJ_REFR
	{
	public:
		// members
		NiPoint3                  angle;            // 00
		NiPoint3                  location;         // 0C
		void* objectReference;  // 18 - ref counted in SetObjectReference vfunc
	};
	static_assert(sizeof(OBJ_REFR) == 0x20);

	// Mirrors only the leading fields of the real LOADED_REF_DATA; data3D is all callers need.
	struct LOADED_REF_DATA {
		void*       handleList;  // 00
		NiAVObject* data3D;      // 08
	};

	class TESObjectREFR {
	public:
		virtual ~TESObjectREFR();  // 00

    char pad8[0x70];
    void* valueChangeEvent;  // 78
		// members
		OBJ_REFR                                      data;           // 80
		char inventoryList[16];  // 98
		TESObjectCELL*                                parentCell;     // A8
		// Real type is BSGuarded<LOADED_REF_DATA*, BSReadWriteLock>: pointer followed by the lock.
		// Reading the pointer without taking the lock is only safe from the same thread the game
		// updates it on; this mirrors the render-thread-only assumption already made elsewhere in
		// this file (see CreationEngineCameraManager::UpdateWorldCamera / onFPSGetCameraRotation).
		LOADED_REF_DATA*                              loadedData;     // B0
		char                                           loadedDataLockPad[8]; // B8
		void*               extraDataList;  // C0
		std::uint16_t                                 scale;          // C8
		std::uint8_t                                  unkE2;          // CA
		std::uint8_t                                  flags;          // CB

		[[nodiscard]] inline NiAVObject* Get3D() const noexcept {
			return loadedData ? loadedData->data3D : nullptr;
		}

	private:
		void AddLockChange();
	};
	static_assert(sizeof(TESObjectREFR) == 0xD8);
  static_assert(offsetof(TESObjectREFR, data) == 0x80);
}
