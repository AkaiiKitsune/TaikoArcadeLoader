#include "constants.h"
#include "helpers.h"
#include "poll.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <queue>

extern GameVersion gameVersion;
extern Keybindings QR_CARD_READ;
extern Keybindings QR_DATA_READ;
extern CardKeybindings *QRCODE_CARDS;
extern size_t QRCODE_CARDS_LENG;

namespace patches::Qr {

enum class State { Ready, CopyWait, AfterCopy1, AfterCopy2 };
enum class Mode { Card, Data, MultiCard };
State gState            = State::Ready;
Mode gMode              = Mode::Card;
std::string gCardNumber = "";

HOOK_DYNAMIC (char, __fastcall, qrInit, i64) { return 1; }
HOOK_DYNAMIC (char, __fastcall, qrRead, i64 a1) {
	*(DWORD *)(a1 + 40) = 1;
	*(DWORD *)(a1 + 16) = 1;
	*(BYTE *)(a1 + 112) = 0;
	return 1;
}
HOOK_DYNAMIC (char, __fastcall, qrClose, i64) { return 1; }
HOOK_DYNAMIC (i64, __fastcall, callQrUnknown, i64) {
	switch (gState) {
	case State::Ready:
	case State::CopyWait: {
		return 1;
	}
	case State::AfterCopy1: {
		gState = State::AfterCopy2;
		return 1;
	}
	case State::AfterCopy2: {
		return 1;
	}
	default: return 0;
	}
}
HOOK_DYNAMIC (bool, __fastcall, Send1, i64, const void *, i64) { return true; }
HOOK_DYNAMIC (bool, __fastcall, Send2, i64, char) { return true; }
HOOK_DYNAMIC (bool, __fastcall, Send3, i64 a1) {
	*(WORD *)(a1 + 88) = 0;
	*(BYTE *)(a1 + 90) = 0;
	return true;
}
HOOK_DYNAMIC (bool, __fastcall, Send4, i64 a1) {
	*(BYTE *)(a1 + 88) = 1;
	*(i64 *)(a1 + 32)  = *(i64 *)(a1 + 24);
	*(WORD *)(a1 + 89) = 0;
	return true;
}
HOOK_DYNAMIC (i64, __fastcall, copy_data, i64, void *dest, int length) {
	if (gState == State::CopyWait) {
		std::cout << "Copy data, length: " << length << std::endl;

		auto configPath      = std::filesystem::current_path () / "config.toml";
		toml_table_t *config = openConfig (configPath);

		if (gMode == Mode::Card) {
			std::string card = "";
			if (config) {
				auto qr = openConfigSection (config, "qr");
				if (qr) card = readConfigString (qr, "card", "");
				toml_free (config);
			}

			memcpy (dest, card.c_str (), card.size () + 1);
			gState = State::AfterCopy1;
			return card.size () + 1;
        } else if (gMode == Mode::MultiCard) {
            if (config) toml_free (config);
            memcpy (dest, gCardNumber.c_str (), gCardNumber.size () + 1);
			gState = State::AfterCopy1;
			return gCardNumber.size () + 1;
		} else {
			std::string serial = "";
			u16 type           = 0;
			std::vector<i64> songNoes;

			if (config) {
				auto qr = openConfigSection (config, "qr");
				if (qr) {
					auto data = openConfigSection (qr, "data");
					if (data) {
						serial   = readConfigString (data, "serial", "");
						type     = readConfigInt (data, "type", 0);
						songNoes = readConfigIntArray (data, "song_no", songNoes);
					}
				}
				toml_free (config);
			}

			BYTE serial_length           = (BYTE)serial.size ();
			std::vector<BYTE> byteBuffer = {0x53, 0x31, 0x32, 0x00, 0x00, 0xFF, 0xFF, serial_length, 0x01, 0x00};

			for (char c : serial)
				byteBuffer.push_back ((BYTE)c);

			if (type == 5) {
				std::vector<BYTE> folderData = {0xFF, 0xFF};

				folderData.push_back (songNoes.size () * 2);

				folderData.push_back ((u8)(type & 0xFF));
				folderData.push_back ((u8)((type >> 8) & 0xFF));

				for (u16 songNo : songNoes) {
					folderData.push_back ((u8)(songNo & 0xFF));
					folderData.push_back ((u8)((songNo >> 8) & 0xFF));
				}

				for (auto c : folderData)
					byteBuffer.push_back (c);
			}

			byteBuffer.push_back (0xEE);
			byteBuffer.push_back (0xFF);

			memcpy (dest, byteBuffer.data (), byteBuffer.size ());
			gState = State::AfterCopy1;
			return byteBuffer.size ();
		}
	}
	return 0;
}

int gCount = 0;

void
Update () {
	if (gState == State::AfterCopy2) {
		gCount++;
		if (gCount > 10) {
			gCount = 0;
			gState = State::Ready;
		}
	}
	if (gState == State::Ready) {
		if (IsButtonTapped (QR_CARD_READ)) {
			std::cout << "Insert" << std::endl;
			gState = State::CopyWait;
			gMode  = Mode::Card;
		} else if (IsButtonTapped (QR_DATA_READ)) {
			std::cout << "Insert" << std::endl;
			gState = State::CopyWait;
			gMode  = Mode::Data;
		} else if (QRCODE_CARDS != nullptr) {
            for (size_t i = 0; i < QRCODE_CARDS_LENG; i++) {
                if (IsButtonTapped (QRCODE_CARDS[i].keybindings)) {
                    std::cout << "Insert" << std::endl;
			        gState      = State::CopyWait;
			        gMode       = Mode::MultiCard;
                    gCardNumber = QRCODE_CARDS[i].card;
                    break;
                }
            }
        }
	}
}

void
Init () {
	auto amHandle = (u64)GetModuleHandle ("AMFrameWork.dll");
	switch (gameVersion) {
	case GameVersion::JP_NOV_2020: {
		INSTALL_HOOK_DYNAMIC (qrInit, (LPVOID)(amHandle + 0x1BA00));
		INSTALL_HOOK_DYNAMIC (qrRead, (LPVOID)(amHandle + 0x1BC20));
		INSTALL_HOOK_DYNAMIC (qrClose, (LPVOID)(amHandle + 0x1BBD0));
		INSTALL_HOOK_DYNAMIC (callQrUnknown, (LPVOID)(amHandle + 0xFD40));
		// 08.18 has no Send1
		INSTALL_HOOK_DYNAMIC (Send2, (LPVOID)(amHandle + 0x1C2D0));
		INSTALL_HOOK_DYNAMIC (Send3, (LPVOID)(amHandle + 0x1C260));
		INSTALL_HOOK_DYNAMIC (Send4, (LPVOID)(amHandle + 0x1C220));
		INSTALL_HOOK_DYNAMIC (copy_data, (LPVOID)(amHandle + 0x1C2A0));
		break;
	}
	case GameVersion::CN_JUN_2023: {
		INSTALL_HOOK_DYNAMIC (qrInit, (LPVOID)(amHandle + 0x161B0));
		INSTALL_HOOK_DYNAMIC (qrRead, (LPVOID)(amHandle + 0x163A0));
		INSTALL_HOOK_DYNAMIC (qrClose, (LPVOID)(amHandle + 0x16350));
		INSTALL_HOOK_DYNAMIC (callQrUnknown, (LPVOID)(amHandle + 0x8F60));
		INSTALL_HOOK_DYNAMIC (Send1, (LPVOID)(amHandle + 0x16A30));
		INSTALL_HOOK_DYNAMIC (Send2, (LPVOID)(amHandle + 0x16A00));
		INSTALL_HOOK_DYNAMIC (Send3, (LPVOID)(amHandle + 0x16990));
		INSTALL_HOOK_DYNAMIC (Send4, (LPVOID)(amHandle + 0x16940));
		INSTALL_HOOK_DYNAMIC (copy_data, (LPVOID)(amHandle + 0x169D0));
		break;
	}
	default: {
		break;
	}
	}
}
} // namespace patches::Qr
