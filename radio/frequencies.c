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

#include "driver/serial-flash.h"
#include "frequencies.h"

FrequencyBandInfo_t gFrequencyBandInfo;
bool gUseUhfFilter;

uint8_t gCurrentFrequencyBand = 0xFF;

uint8_t gTxPowerLevelHigh = 40;
uint8_t gTxPowerLevelLow = 20;
#ifndef ENABLE_ALT_SQUELCH
uint8_t gSquelchNoiseWide = 40;
uint8_t gSquelchNoiseNarrow = 40;
uint8_t gSquelchRSSIWide = 80;
uint8_t gSquelchRSSINarrow = 85;
#endif

uint32_t FREQUENCY_GetStep(uint8_t StepSetting)
{
#if 0 // original setting
	switch(StepSetting) {
		case  0: return 25;
		case  1: return 125;
		case  2: return 250;
		case  3: return 500;
		case  4: return 625;
		case  5: return 1000;
		case  6: return 1250;
		case  7: return 2000;
		case  8: return 2500;
		case  9: return 5000;
		case 10: return 10000;
		case 11: return 50000;
		case 12: return 100000;
		case 13: return 500000;
		case 14: return 1;
		default: return 25;
	}
#endif
	switch(StepSetting) {
		// values * 10Hz
		case  0: return 1; 		//     10 Hz
		case  1: return 25; 	//    250 Hz
		case  2: return 125;	//   1250 Hz
		case  3: return 250;	//   2500 Hz
		case  4: return 500;	//  5.00 kHz
		case  5: return 625;	//  6.25 kHz
		case  6: return 833;	//  8.33 kHz
		case  7: return 1000;	// 10.00 kHz
		case  8: return 1250;	// 12.50 kHz
		case  9: return 2000;	// 20.00 kHz
		case 10: return 2500;	// 25.00 kHz
		case 11: return 5000;	// 50.00 kHz
		case 12: return 10000;	//100.00 kHz
		case 13: return 50000;	//500.00 kHz
		case 14: return 100000;	//  1.00 MHz
		case 15: return 500000;	//  5.00 MHz
		default: return 2500;	// 25.00 kHz

	}
}

void FREQUENCY_SelectBand(uint32_t Frequency)
{
	uint8_t Band;
	uint8_t Level;

	if (Frequency >= 13600000  && Frequency <= 17400000) {
		Band = BAND_136MHz;
		Level = (Frequency - 13500000) / 500000;
		gUseUhfFilter = false;
	} else if (Frequency >= 40000000 && Frequency <= 48000000) {
		Band = BAND_400MHz;
		Level = (Frequency - 40000000) / 500000;
		gUseUhfFilter = true;
	} else if (Frequency >= 6400000 && Frequency <= 13600000) {
		Band = BAND_64MHz;
		Level = (Frequency - 6000000) / 500000;
		gUseUhfFilter = false;
	} else if (Frequency >= 17400000 && Frequency <= 24000000) {
		Band = BAND_174MHz;
		Level = (Frequency - 17000000) / 500000;
		gUseUhfFilter = false;
	} else if (Frequency >= 24000000 && Frequency <= 32000000) {
		Band = BAND_240MHz;
		Level = (Frequency - 24000000) / 500000;
		gUseUhfFilter = true;
	} else if (Frequency >= 32000000 && Frequency <= 40000000) {
		Band = BAND_320MHz;
		Level = (Frequency - 32000000) / 500000;
		gUseUhfFilter = true;
	} else if (Frequency >= 48000000 && Frequency <= 56000000) {
		Band = BAND_480MHz;
		Level = (Frequency - 48000000) / 500000;
		gUseUhfFilter = true;
	} else {
		return;
	}

	if (Band != gCurrentFrequencyBand) {
		gCurrentFrequencyBand = Band;
		SFLASH_Read(&gFrequencyBandInfo, 0x3BF020 + (Band * sizeof(gFrequencyBandInfo)), sizeof(gFrequencyBandInfo));
	}
	if (Level > 15) {
		Level = 15;
	}
#ifndef ENABLE_ALT_SQUELCH
	gSquelchNoiseWide = gFrequencyBandInfo.SquelchNoiseWide[Level];
	gSquelchRSSIWide = gFrequencyBandInfo.SquelchRSSIWide[Level];
	if (Band == BAND_64MHz) {
		gSquelchNoiseWide += 10;
		gSquelchRSSIWide -= 10;
	}
#endif
	gTxPowerLevelLow = gFrequencyBandInfo.TxPowerLevelLow[Level];
	gTxPowerLevelHigh = gFrequencyBandInfo.TxPowerLevelHigh[Level];
#ifndef ENABLE_ALT_SQUELCH
	gSquelchNoiseNarrow = gFrequencyBandInfo.SquelchNoiseNarrow[Level];
	gSquelchRSSINarrow = gFrequencyBandInfo.SquelchRSSINarrow[Level];
#endif
}

