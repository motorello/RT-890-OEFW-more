/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "app/radio.h"
#include "app/regedit.h"
#include "driver/bk4819.h"
#include "driver/key.h"
#include "helper/helper.h"
#include "radio/settings.h"
#include "task/vox.h"
#include "ui/gfx.h"
#include "ui/helper.h"
#include "ui/main.h"
#include "driver/delay.h"
#include "misc.h"

#ifdef UART_DEBUG
	#include "driver/uart.h"
	#include "external/printf/printf.h"
#endif

static const Registers RegisterTable[] = {
    {"LNAS ", 0x10,  8, 0b11,  1},
    {"LNA  ", 0x10,  5, 0b111, 1},
	{"MIX  ", 0x10,  3, 0b11,  1},
    {"PGA  ", 0x10,  0, 0b111, 1},
    {"BW   ", 0x43, 12, 0b111, 1},
    {"WEAK ", 0x43,  9, 0b111, 1},
};

static const uint8_t RegCount = sizeof(RegisterTable) / sizeof(RegisterTable[0]);

static uint8_t bExit;
static uint8_t RegIndex = 1;
static uint16_t RegValue;
static uint16_t SettingValue;
static uint8_t CurrentReg; 

void UI_DrawStatusRegedit(uint8_t Vfo, uint32_t Frequency) {

	gColorForeground = COLOR_GREY;
	// write current vfo
	UI_DrawSmallString(2, 86, "VFO", 3);
	UI_DrawSmallCharacter(2 + 4*5, 86, '0' + Vfo+1);
	//UI_DrawSmallCharacter(2 + 5*5 , 86, '-');
	
	// write current frequency
	Int2Ascii(Frequency, 8);
	for(uint8_t i = 7; i > 2; i--) {
		gShortString[i+1] = gShortString[i];
	}
	gShortString[3] = '.';	
	UI_DrawSmallString(160 - 9*6, 86, gShortString, 9);
}

void UI_DrawBarRegedit(uint8_t Level)
{
	uint8_t Y = 10;
	uint8_t i;

    if (Level < 33) {
		gColorForeground = COLOR_RGB(31, 62,  0);
	} else if (Level < 66) {
		gColorForeground = COLOR_RGB(31, 41,  0);
	} else {
		gColorForeground = COLOR_RGB(31, 29,  0);
	}

	for (i = 0; i < Level; i++) {
	if (i != 0 && i != 33 && i != 66 && i != 99) {
			DISPLAY_DrawRectangle1(15 + i, Y, 4, 1, gColorForeground);
		}
	}

    for (; Level < 100; Level++) {
		if (Level != 0 && Level != 33 && Level != 66 && Level != 99) {
			DISPLAY_DrawRectangle1(15 + Level, Y, 4, 1, gColorBackground);
		}
	}
}

void UI_DrawRxDbmRegedit(bool Clear)
{
	uint8_t Y = 10;
		
	gColorForeground = COLOR_FOREGROUND;
	if (Clear) {
		UI_DrawSmallString(120, Y, "     ", 5);
	} else {
		UI_DrawSmallString(120, Y, gShortString, 4);
	}
}

static void CheckRSSIRegedit(void)
{
	if (gVoxRssiUpdateTimer != 0) {
		return;
	}

#ifdef ENABLE_SLOWER_RSSI_TIMER
		gVoxRssiUpdateTimer = 500;
#else
		gVoxRssiUpdateTimer = 100;
#endif

	uint16_t RSSI;
	uint16_t Power;

	RSSI = BK4819_GetRSSI();

	//Valid range is 72 - 330
	if (RSSI < 72) {
		Power = 0;
	} else if (RSSI > 330) {
		Power = 100;
	} else {
		Power = ((RSSI-72)*100)/258;
	}
	
	UI_DrawBarRegedit(Power);
	ConvertRssiToDbm(RSSI);
	UI_DrawRxDbmRegedit(false);
}


void RegeditDrawStatusBar(uint8_t Vfo)
{
	DISPLAY_Fill(0, 159, 0, 96, COLOR_BACKGROUND);
	//DISPLAY_DrawRectangle0(0, 82, 160, 1, gSettings.BorderColor);
	DISPLAY_DrawRectangle0(0, 82, 160, 1, COLOR_GREY);

	UI_DrawStatusRegedit(Vfo, gVfoInfo[Vfo].Frequency);
}

void ChangeRegValue(uint8_t bUp) {

    uint16_t FullMask;

    if (bUp) {
        SettingValue = (SettingValue + 1) % (RegisterTable[RegIndex].Mask + 1);
    } else {
        SettingValue = (SettingValue + (RegisterTable[RegIndex].Mask + 1) - 1) % (RegisterTable[RegIndex].Mask + 1);
    }

    FullMask = RegisterTable[RegIndex].Mask << RegisterTable[RegIndex].Offset;

    SettingValue <<= RegisterTable[RegIndex].Offset;
    RegValue = (RegValue & ~FullMask) | SettingValue;

    BK4819_WriteRegister(CurrentReg, RegValue);

}

void RegEditCheckKeys(void) {
	KEY_t Key;
	static KEY_t LastKey;

	Key = KEY_GetButton();

	if (Key != LastKey) {
		switch (Key) {
			case KEY_NONE:
				break;
			case KEY_EXIT:
				bExit = true;
				break;
			case KEY_UP:
				//RegIndex = (RegIndex + 1) % RegCount;
				RegIndex = (RegIndex + RegCount - 1) % RegCount;
				break;
			case KEY_DOWN:
				RegIndex = (RegIndex + 1) % RegCount;
				//RegIndex = (RegIndex + RegCount - 1) % RegCount;
				break;
            case KEY_1:
                BK4819_ToggleAGCMode();
                break;
            case KEY_2:
				ChangeRegValue(false);
				break;
            case KEY_3:
				ChangeRegValue(true);
				break;
			default:
				break;
		}
		LastKey = Key;
	}
}

void APP_RegEdit(void) {

    uint16_t ActiveGainReg;

	//RADIO_EndAudio();  // Just in case audio is open

	DISPLAY_Fill(0, 159, 1, 96, COLOR_BACKGROUND);

	RegeditDrawStatusBar(gCurrentVfo);

    bExit = false;
	gScreenMode = SCREEN_REGEDIT;

    while (1) {
        RegEditCheckKeys();

        if (bExit){
			gScreenMode = SCREEN_MAIN;
            UI_DrawMain(false);
            return;
        }

        UI_DrawVoltage(0);

        ActiveGainReg = BK4819_ReadRegister(0x7e);
        ActiveGainReg >>= 12;
        ActiveGainReg &= 0b111;

        CurrentReg = (RegIndex < 4) ? RegisterTable[RegIndex].RegAddr + ActiveGainReg : RegisterTable[RegIndex].RegAddr;

        RegValue = BK4819_ReadRegister(CurrentReg);

        SettingValue = RegValue >> RegisterTable[RegIndex].Offset;
        SettingValue &= RegisterTable[RegIndex].Mask;

        gColorForeground = COLOR_FOREGROUND;
        UI_DrawString(50, 40, RegisterTable[RegIndex].Name, 5);
        gShortString[1] = ' ';
        Int2Ascii(SettingValue, RegisterTable[RegIndex].DiplayDigits);
        UI_DrawString(100, 40, gShortString, 2);

        if (gRadioMode == RADIO_MODE_RX) {
        	// for the moment, only display the s-meter if squelch is open, otherwise squelch does not open upon receiving
        	CheckRSSIRegedit();
        } else {
        	UI_DrawSmallString((160 - 14*6)/2, 10, "SQUELCH CLOSED", 14);
        }

        DELAY_WaitMS(100);
	}
}