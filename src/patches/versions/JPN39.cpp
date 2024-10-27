#include "../patches.h"
#include "helpers.h"
#include <safetyhook.hpp>
#include <iostream>

namespace patches::JPN39 {

HOOK_DYNAMIC (char, __fastcall, AMFWTerminate, i64) { return 0; }

HOOK_DYNAMIC (i64, __fastcall, curl_easy_setopt, i64 a1, i64 a2, i64 a3, i64 a4, i64 a5) {
    // printf ("garmc curl_easy_setopt\n");
    originalcurl_easy_setopt (a1, 64, 0, 0, 0);
    originalcurl_easy_setopt (a1, 81, 0, 0, 0);
    return originalcurl_easy_setopt (a1, a2, a3, a4, a5);
}

FUNCTION_PTR (i64, lua_settop, PROC_ADDRESS ("lua51.dll", "lua_settop"), u64, u64);
FUNCTION_PTR (i64, lua_replace, PROC_ADDRESS ("lua51.dll", "lua_replace"), u64, u64);
FUNCTION_PTR (i64, lua_pushboolean, PROC_ADDRESS ("lua51.dll", "lua_pushboolean"), u64, u64);
FUNCTION_PTR (i64, lua_pushstring, PROC_ADDRESS ("lua51.dll", "lua_pushstring"), u64, u64);
FUNCTION_PTR (i64, lua_pushcclosure, PROC_ADDRESS ("lua51.dll", "lua_pushcclosure"), u64, u64, u64);

FUNCTION_PTR (i64, lua_toboolean, PROC_ADDRESS ("lua51.dll", "lua_toboolean"), u64, u64);
FUNCTION_PTR (i64, lua_tolstring, PROC_ADDRESS ("lua51.dll", "lua_tolstring"), u64, u64, u64);

i64
lua_pushtrue (i64 a1) {
    lua_settop (a1, 0);
    lua_pushboolean (a1, 1);
    return 1;
}

HOOK (i64, AvailableMode_Collabo024, ASLR (0x1402DE710), i64 a1) { return lua_pushtrue (a1); }
HOOK (i64, AvailableMode_Collabo025, ASLR (0x1402DE6B0), i64 a1) { return lua_pushtrue (a1); }
HOOK (i64, AvailableMode_Collabo026, ASLR (0x1402DE670), i64 a1) { return lua_pushtrue (a1); }
HOOK (i64, AvailableMode_AprilFool001, ASLR (0x1402DE5B0), i64 a1) { return lua_pushtrue (a1); }

const i32 datatableBufferSize = 1024 * 1024 * 12;
safetyhook::Allocation datatableBuffer1;
safetyhook::Allocation datatableBuffer2;
safetyhook::Allocation datatableBuffer3;
const std::vector<uintptr_t> datatableBuffer1Addresses = {0x1400ABE40, 0x1400ABEB1, 0x1400ABEDB, 0x1400ABF4C};
const std::vector<uintptr_t> datatableBuffer2Addresses = {0x1400ABE2C, 0x1400ABF5B, 0x1400ABF8E};
const std::vector<uintptr_t> datatableBuffer3Addresses = {0x1400ABF7F, 0x1400ABF95, 0x1400ABFBF};
const std::vector<uintptr_t> memsetSizeAddresses       = {0x1400ABE26, 0x1400ABE3A, 0x1400ABF79};

void
AllocateStaticBufferNear (void *target_address, size_t size, safetyhook::Allocation *newBuffer) {
    auto allocator                = safetyhook::Allocator::global ();
    std::vector desired_addresses = {(uint8_t *)target_address};
    auto allocation_result        = allocator->allocate_near (desired_addresses, size);
    if (allocation_result.has_value ()) *newBuffer = std::move (*allocation_result);
}

void
ReplaceLeaBufferAddress (const std::vector<uintptr_t> &bufferAddresses, void *newBufferAddress) {
    for (auto bufferAddress : bufferAddresses) {
        uintptr_t lea_instruction_dst = ASLR (bufferAddress) + 3;
        uintptr_t lea_instruction_end = ASLR (bufferAddress) + 7;
        intptr_t offset               = (intptr_t)newBufferAddress - lea_instruction_end;
        WRITE_MEMORY (lea_instruction_dst, i32, (i32)offset);
    }
}

// -------------- MidHook Area --------------

HOOK_MID (ChangeLanguageType, ASLR (0x1400B2016), SafetyHookContext &ctx) {
    int *pFontType = (int *)ctx.rax;
    if (*pFontType == 4) *pFontType = 2;
}

i64 __fastcall
lua_freeze_timer (i64 a1) {
    return lua_pushtrue (a1);
}

HOOK_MID (FreezeTimer, ASLR (0x14019FF51), SafetyHookContext &ctx) {
    auto a1 = ctx.rdi;
    int v9 = (int)(ctx.rax + 1);
    lua_pushcclosure(a1, reinterpret_cast<i64>(&lua_freeze_timer), v9);
    ctx.rip = ASLR (0x14019FF65);
}

std::map<std::string, int> nus3bankMap;
int nus3bankIdCounter = 0;
std::map<std::string, bool> voiceCnExist;
bool enableSwitchVoice = false;
std::mutex nus3bankMtx;

int
get_bank_id(std::string bankName) {
    if (nus3bankMap.find(bankName) == nus3bankMap.end()) {
        nus3bankMap[bankName] = nus3bankIdCounter;
        nus3bankIdCounter++;
    }
    return nus3bankMap[bankName];
}

void
check_voice_tail(std::string bankName, uint8_t* pBinfBlock, std::map<std::string, bool> &voiceExist, std::string tail) {
    // check if any voice_xxx.nus3bank has xxx_cn audio inside while loading
    if (enableSwitchVoice && bankName.starts_with("voice_")) {
        int binfLength = *((int*)(pBinfBlock + 4));
        uint8_t* pGrpBlock = pBinfBlock + 8 + binfLength;
        int grpLength = *((int*)(pGrpBlock + 4));
        uint8_t* pDtonBlock = pGrpBlock + 8 + grpLength;
        int dtonLength = *((int*)(pDtonBlock + 4));
        uint8_t* pToneBlock = pDtonBlock + 8 + dtonLength;
        int toneSize = *((int*)(pToneBlock + 8));
        uint8_t* pToneBase = pToneBlock + 12;
        for (int i = 0; i < toneSize; i++) {
            if (*((int*)(pToneBase + i * 8 + 4)) <= 0x0C) continue;     // skip empty space
            uint8_t* currToneBase = pToneBase + *((int*)(pToneBase + i * 8));
            int titleOffset = -1;
            switch (*currToneBase) {
                case 0xFF: titleOffset = 9; break;      // audio mark
                case 0x7F: titleOffset = 5; break;      // randomizer mark
                default: continue;                      // unknown mark skip
            }
            if (titleOffset > 0) {
                std::string title((char*)(currToneBase + titleOffset));
                if (title.ends_with(tail)) {
                    if (voiceExist.find(bankName) == voiceExist.end() || !voiceExist[bankName]) {
                        voiceExist[bankName] = true;
                    }
                    return;
                }
            }
        }
        if (voiceExist.find(bankName) == voiceExist.end() || voiceExist[bankName]) {
            voiceExist[bankName] = false;
        }
    }
}

HOOK_MID (GenNus3bankId, ASLR (0x1407B97BD), SafetyHookContext &ctx) {
    std::lock_guard<std::mutex> lock(nus3bankMtx);
    if ((uint8_t**)(ctx.rcx + 8) != nullptr) {
        uint8_t* pNus3bankFile = *((uint8_t **)(ctx.rcx + 8));
        if (pNus3bankFile[0] == 'N' && pNus3bankFile[1] == 'U' && pNus3bankFile[2] == 'S' && pNus3bankFile[3] == '3') {
            int tocLength = *((int*)(pNus3bankFile + 16));
            uint8_t* pPropBlock = pNus3bankFile + 20 + tocLength;
            int propLength = *((int*)(pPropBlock + 4));
            uint8_t* pBinfBlock = pPropBlock + 8 + propLength;
            std::string bankName((char*)(pBinfBlock + 0x11));
            check_voice_tail(bankName, pBinfBlock, voiceCnExist, "_cn");
            ctx.rax = get_bank_id(bankName);
        }
    }
}


// -------------- MidHook Area End --------------

int language = 0;
const char *
languageStr () {
    switch (language) {
    case 1: return "en_us";
    case 2: return "cn_tw";
    case 3: return "kor";
    case 4: return "cn_cn";
    default: return "jpn";
    }
}
HOOK (i64, GetLanguage, ASLR (0x140024AC0), i64 a1) {
    auto result = originalGetLanguage (a1);
    language    = *((u32 *)result);
    return result;
}
HOOK (i64, GetRegionLanguage, ASLR (0x1401CE9B0), i64 a1) {
    lua_settop (a1, 0);
    lua_pushstring (a1, (u64)languageStr ());
    return 1;
}
HOOK (i64, GetCabinetLanguage, ASLR (0x1401D1A60), i64, i64 a2) {
    lua_settop (a2, 0);
    lua_pushstring (a2, (u64)languageStr ());
    return 1;
}

std::string 
fixToneName (std::string bankName, std::string toneName) {
    if (enableSwitchVoice && bankName.starts_with("voice_")) {
        if (language == 2 || language == 4) {
            bool existCnVoice = voiceCnExist.find(bankName) != voiceCnExist.end() && voiceCnExist[bankName];
            if (existCnVoice) {
                return toneName + "_cn";
            }
        }
    }
    return toneName;
}

HOOK (i64, PlaySound, ASLR (0x1404C6DC0), i64 a1) {
    if (enableSwitchVoice && language != 0) {
        int size_bankName = 0;
        const char* bankName = (char*)lua_tolstring (a1, -3, (u64)&size_bankName);
        int size_toneName = 0;
        const char* toneName = (char*)lua_tolstring (a1, -2, (u64)&size_toneName);
        lua_pushstring (a1, (u64)(fixToneName (bankName, toneName).c_str()));
        lua_replace(a1, -3);
    }
    return originalPlaySound(a1);
}

HOOK (i64, PlaySoundMulti, ASLR (0x1404C6DC0), i64 a1) {
    if (enableSwitchVoice && language != 0) {
        int size_bankName = 0;
        const char* bankName = (char*)lua_tolstring (a1, -3, (u64)&size_bankName);
        int size_toneName = 0;
        const char* toneName = (char*)lua_tolstring (a1, -2, (u64)&size_toneName);
        lua_pushstring (a1, (u64)(fixToneName (bankName, toneName).c_str()));
        lua_replace (a1, -3);
    }
    return originalPlaySoundMulti(a1);
}

HOOK_MID (PlaySoundInEnso, ASLR (0x1404ED5F9), SafetyHookContext &ctx) {
    char* originalPlaySound = *((char**)ctx.rax);
    std::string playSound(originalPlaySound);
    if (enableSwitchVoice && language != 0 && playSound[0] == 'v') {
        size_t slashIndex = playSound.find("/");
        if (slashIndex != std::string::npos) {
            std::string bankName = playSound.substr(0, slashIndex);
            if (language == 2 || language == 4) {
                if (voiceCnExist.find(bankName) != voiceCnExist.end() && voiceCnExist[bankName]) {
                    std::string finalPlaySound = playSound + "_cn";
                    ctx.rax = (uintptr_t)((void*)&finalPlaySound);
                }
            }
        }
    }
}

HOOK_MID (PlaySoundInAiEnso, ASLR (0x1404ED296), SafetyHookContext &ctx) {
    char* originalPlaySound = *((char**)ctx.rax);
    std::string playSound(originalPlaySound);
    if (enableSwitchVoice && language != 0 && playSound[0] == 'v') {
        size_t slashIndex = playSound.find("/");
        if (slashIndex != std::string::npos) {
            std::string bankName = playSound.substr(0, slashIndex);
            if (language == 2 || language == 4) {
                if (voiceCnExist.find(bankName) != voiceCnExist.end() && voiceCnExist[bankName]) {
                    std::string finalPlaySound = playSound + "_cn";
                    ctx.rax = (uintptr_t)((void*)&finalPlaySound);
                }
            }
        }
    }
}

int loaded_fail_count = 0;
HOOK (i64, LoadedBankAll, ASLR (0x1404C69F0), i64 a1) {
    originalLoadedBankAll (a1);
    auto result = lua_toboolean(a1, -1);
    lua_settop(a1, 0);
    if (result) {
        loaded_fail_count = 0;
        lua_pushboolean (a1, 1);
    } else if (loaded_fail_count > 100) {
        loaded_fail_count = 0;
        lua_pushboolean (a1, 1);
    } else {
        loaded_fail_count += 1;
        lua_pushboolean (a1, 0);
    }
    return 1;
}

void
Init () {
    i32 xRes              = 1920;
    i32 yRes              = 1080;
    bool unlockSongs      = true;
    bool fixLanguage      = false;
    bool freezeTimer      = false;
    bool chsPatch         = false;
    bool modeCollabo025   = false;
    bool modeCollabo026   = false;
    bool modeAprilFool001 = false;

    bool useLayeredfs     = false;

    auto configPath = std::filesystem::current_path () / "config.toml";
    std::unique_ptr<toml_table_t, void (*) (toml_table_t *)> config_ptr (openConfig (configPath), toml_free);
    if (config_ptr) {
        auto patches = openConfigSection (config_ptr.get (), "patches");
        if (patches) {
            unlockSongs = readConfigBool (patches, "unlock_songs", unlockSongs);
            auto jpn39  = openConfigSection (patches, "jpn39");
            if (jpn39) {
                fixLanguage      = readConfigBool (jpn39, "fix_language", fixLanguage);
                freezeTimer      = readConfigBool (jpn39, "freeze_timer", freezeTimer);
                chsPatch         = readConfigBool (jpn39, "chs_patch", chsPatch);
                modeCollabo025   = readConfigBool (jpn39, "mode_collabo025", modeCollabo025);
                modeCollabo026   = readConfigBool (jpn39, "mode_collabo026", modeCollabo026);
                modeAprilFool001 = readConfigBool (jpn39, "mode_aprilfool001", modeAprilFool001);
            }
        }

        auto graphics = openConfigSection (config_ptr.get (), "graphics");
        if (graphics) {
            auto res = openConfigSection (graphics, "res");
            if (res) {
                xRes = readConfigInt (res, "x", xRes);
                yRes = readConfigInt (res, "y", yRes);
            }
        }

        auto layeredfs = openConfigSection (config_ptr.get (), "layeredfs");
        if (layeredfs) {
            useLayeredfs = readConfigBool (layeredfs, "enabled", useLayeredfs);
        }
    }

    // Apply common config patch
    WRITE_MEMORY (ASLR (0x140494533), i32, xRes);
    WRITE_MEMORY (ASLR (0x14049453A), i32, yRes);
    if (unlockSongs) WRITE_MEMORY (ASLR (0x1403F45CF), u8, 0xB0, 0x01);

    // Bypass errors
    WRITE_MEMORY (ASLR (0x140041A00), u8, 0xC3);

    // Use TLS v1.2
    WRITE_MEMORY (ASLR (0x140580459), u8, 0x10);

    // Disable F/G check
    WRITE_MEMORY (ASLR (0x140CD4858), char, "./");

    // Move various files to current directory
    WRITE_MEMORY (ASLR (0x140C8AB60), char, "./"); // F:/
    WRITE_MEMORY (ASLR (0x140C8AB5C), char, "./"); // G:/
    WRITE_MEMORY (ASLR (0x140CD4DC0), char, ".\\Setting1.bin");
    WRITE_MEMORY (ASLR (0x140CD4DB0), char, ".\\Setting2.bin");
    WRITE_MEMORY (ASLR (0x140C95B40), char, ".\\TournamentData\\PlayData\\TournamentPlayData.dat");
    WRITE_MEMORY (ASLR (0x140C95B78), char, ".\\TournamentData\\PlayData\\TournamentPlayData.dat");
    WRITE_MEMORY (ASLR (0x140C95BB0), char, ".\\TournamentData\\InfoData\\TournamentInfoData.dat");
    WRITE_MEMORY (ASLR (0x140C95BE8), char, ".\\TournamentData\\InfoData\\TournamentInfoData.dat");
    WRITE_MEMORY (ASLR (0x140CC0508), char, ".\\Garmc\\BillingData\\GarmcBillingData.dat");
    WRITE_MEMORY (ASLR (0x140CC0538), char, ".\\Garmc\\BillingData\\GarmcBillingData.dat");
    WRITE_MEMORY (ASLR (0x140CC0660), char, ".\\Garmc\\BillingData\\GarmcOBillingData.dat");
    WRITE_MEMORY (ASLR (0x140CC0690), char, ".\\Garmc\\BillingData\\GarmcOBillingData.dat");
    WRITE_MEMORY (ASLR (0x140CC05E0), char, ".\\Garmc\\BillingNetIdLocationId\\GarmcBillingNetIdLocationId.dat");
    WRITE_MEMORY (ASLR (0x140CC0620), char, ".\\Garmc\\BillingNetIdLocationId\\GarmcBillingNetIdLocationId.dat");
    WRITE_MEMORY (ASLR (0x140CC0830), char, ".\\Garmc\\BillingNetIdLocationId\\GarmcOBillingNetIdLocationId.dat");
    WRITE_MEMORY (ASLR (0x140CC0880), char, ".\\Garmc\\BillingNetIdLocationId\\GarmcOBillingNetIdLocationId.dat");
    WRITE_MEMORY (ASLR (0x140CC06C0), char, ".\\Garmc\\ErrorLogData\\GarmcErrorLogData.dat");
    WRITE_MEMORY (ASLR (0x140CC06F0), char, ".\\Garmc\\ErrorLogData\\GarmcErrorLogData.dat");
    WRITE_MEMORY (ASLR (0x140CC0580), char, ".\\Garmc\\ErrorLogData\\GarmcOErrorLogData.dat");
    WRITE_MEMORY (ASLR (0x140CC05B0), char, ".\\Garmc\\ErrorLogData\\GarmcOErrorLogData.dat");

    // Remove datatable size limit
    {
        for (auto address : memsetSizeAddresses)
            WRITE_MEMORY (ASLR (address) + 2, i32, datatableBufferSize);

        auto bufferBase = MODULE_HANDLE - 0x03000000;
        AllocateStaticBufferNear ((void *)bufferBase, datatableBufferSize, &datatableBuffer1);
        bufferBase += datatableBufferSize;
        AllocateStaticBufferNear ((void *)bufferBase, datatableBufferSize, &datatableBuffer2);
        bufferBase += datatableBufferSize;
        AllocateStaticBufferNear ((void *)bufferBase, datatableBufferSize, &datatableBuffer3);

        ReplaceLeaBufferAddress (datatableBuffer1Addresses, datatableBuffer1.data ());
        ReplaceLeaBufferAddress (datatableBuffer2Addresses, datatableBuffer2.data ());
        ReplaceLeaBufferAddress (datatableBuffer3Addresses, datatableBuffer3.data ());
    }

    // Freeze Timer
    if (freezeTimer) {
        INSTALL_HOOK_MID (FreezeTimer);
    }

    // Use chs font/wordlist instead of cht
    if (chsPatch) {
        bool fontExistAll = true;
        const char* fontToCheck[] {"cn_30.nutexb", "cn_30.xml", "cn_32.nutexb", "cn_32.xml", "cn_64.nutexb", "cn_64.xml"};
        for (int i = 0; i < 6; i++) {
            if (useLayeredfs && std::filesystem::exists(std::string("..\\..\\Data_mods\\x64\\font\\") + fontToCheck[i])) continue;
            if (std::filesystem::exists(std::string("..\\..\\Data\\x64\\font\\") + fontToCheck[i])) continue;
            fontExistAll = false;
        }

        if (fontExistAll) {
            WRITE_MEMORY (ASLR (0x140CD1AE0), char, "cn_64");
            WRITE_MEMORY (ASLR (0x140CD1AF0), char, "cn_32");
            WRITE_MEMORY (ASLR (0x140CD1AF8), char, "cn_30");
            WRITE_MEMORY (ASLR (0x140C946A0), char, "chineseSText");
            WRITE_MEMORY (ASLR (0x140C946B0), char, "chineseSFontType");
            INSTALL_HOOK_MID (ChangeLanguageType);
        }
    }

    // Fix language
    if (fixLanguage) {
        INSTALL_HOOK (GetLanguage);         // Language will use in other place
        INSTALL_HOOK (GetRegionLanguage);
        INSTALL_HOOK (GetCabinetLanguage);
    }

    // if both chsPatch & fixLanguage enabled, try use cn voice from nus3bank
    // don't worry, we will check if cn voice existed
    if (chsPatch && fixLanguage) {
        enableSwitchVoice = true;
        INSTALL_HOOK (PlaySound);
        INSTALL_HOOK (PlaySoundMulti);
        INSTALL_HOOK_MID (PlaySoundInEnso);
        INSTALL_HOOK_MID (PlaySoundInAiEnso);
    }

    // Mode unlock
    if (modeCollabo025) INSTALL_HOOK (AvailableMode_Collabo025);
    if (modeCollabo026) INSTALL_HOOK (AvailableMode_Collabo026);
    if (modeAprilFool001) INSTALL_HOOK (AvailableMode_AprilFool001);

    // Fix normal song play after passing through silent song
    INSTALL_HOOK_MID (GenNus3bankId);
    INSTALL_HOOK (LoadedBankAll);

    // Disable live check
    auto amHandle = (u64)GetModuleHandle ("AMFrameWork.dll");
    INSTALL_HOOK_DYNAMIC (AMFWTerminate, (void *)(amHandle + 0x42DE0));

    // Move various files to current directory
    WRITE_MEMORY (amHandle + 0x15252, u8, 0xEB); // CreditLogPathA
    WRITE_MEMORY (amHandle + 0x15419, u8, 0xEB); // CreditLogPathB
    WRITE_MEMORY (amHandle + 0x416DA, u8, 0xEB); // ErrorLogPathA
    WRITE_MEMORY (amHandle + 0x41859, u8, 0xEB); // ErrorLogPathB
    WRITE_MEMORY (amHandle + 0x41C21, u8, 0xEB); // CommonLogPathA
    WRITE_MEMORY (amHandle + 0x41DA5, u8, 0xEB); // CommonLogPathB
    WRITE_NOP (amHandle + 0x420F1, 0x05);        // BackupDataPathA
    WRITE_NOP (amHandle + 0x42167, 0x05);        // BackupDataPathB

    // Redirect garmc requests
    auto garmcHandle = (u64)GetModuleHandle ("garmc.dll");
    INSTALL_HOOK_DYNAMIC (curl_easy_setopt, (void *)(garmcHandle + 0x1FBBB0));
}
} // namespace patches::JPN39
