
/* Copyright 2023 OneOfEleven
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

// code to 'try' and reduce the AM demodulator saturation problem
//
// that is until someone works out how to properly configure the BK chip !

#include "task/am-fix.h"
#include "app/radio.h"
#include "driver/bk4819.h"
#include "radio/settings.h"
#include "misc.h"

//#include "driver/pins.h" // for testing only -> gpio_set

#ifdef ENABLE_AM_FIX

#define CAP_DBM (-82)
#define STANDBY_INDEX_RELATIVE_TO_MAX (-7)

uint8_t gAmFixIndex;
uint16_t gAmFixCountdown;
int8_t gAmFixCapDbm = CAP_DBM;
uint8_t gAmFixStandbyIndex;

typedef struct
{
	uint16_t reg_val;
	int8_t   gain_dB;
} __attribute__((packed)) t_gain_table;

// REG_10 AGC gain table
//
// <15:10> ???
//
//   <9:8> = LNA Gain Short
//           3 =   0dB   < original value
//           2 = -24dB   // was -11
//           1 = -30dB   // was -16
//           0 = -33dB   // was -19
//
//   <7:5> = LNA Gain
//           7 =   0dB
//           6 =  -2dB
//           5 =  -4dB
//           4 =  -6dB
//           3 =  -9dB
//           2 = -14dB   < original value
//           1 = -19dB
//           0 = -24dB
//
//   <4:3> = MIXER Gain
//           3 =   0dB   < original value
//           2 =  -3dB
//           1 =  -6dB
//           0 =  -8dB
//
//   <2:0> = PGA Gain
//           7 =   0dB
//           6 =  -3dB   < original value
//           5 =  -6dB
//           4 =  -9dB
//           3 = -15dB
//           2 = -21dB
//           1 = -27dB
//           0 = -33dB

// front end register dB values
//
// these values need to be accurate for the code to properly/reliably switch
// between table entries when adjusting the front end registers.
//
// these 4 tables need a measuring/calibration update
//
//
// QUESTION: why do I have to surround the negative numbers in brackets ???
//           if I don't add the brackets, reading the table returns unexpected/different values !!!
//
//
//	static const int16_t lna_short_dB[] = {  -19,   -16,   -11,     0};   // was (but wrong)
//	static const int16_t lna_short_dB[] = { (-33), (-30), (-24),    0};   // corrected'ish
//	static const int16_t lna_dB[]       = { (-24), (-19), (-14), ( -9), (-6), (-4), (-2), 0};
//	static const int16_t mixer_dB[]     = { ( -8), ( -6), ( -3),    0};
//	static const int16_t pga_dB[]       = { (-33), (-27), (-21), (-15), (-9), (-6), (-3), 0};

// lookup table is hugely easier than writing code to do the same
//
#if 0
static const t_gain_table gain_table[] =
{
	{0x03BE, -7},          //        3 5 3 6 ..   0dB   -4dB 0dB  -3dB ..  -7dB original
	{0x0000, -98},         //   1 .. 0 0 0 0 .. -33dB -24dB -8dB -33dB .. -98dB
	{0x0008, -96},         //   2 .. 0 0 1 0 .. -33dB -24dB -6dB -33dB .. -96dB
	{0x0100, -95},         //   3 .. 1 0 0 0 .. -30dB -24dB -8dB -33dB .. -95dB
	{0x0020, -93},         //   4 .. 0 1 0 0 .. -33dB -19dB -8dB -33dB .. -93dB
	{0x0001, -92},         //   5 .. 0 0 0 1 .. -33dB -24dB -8dB -27dB .. -92dB
	{0x0028, -91},         //   6 .. 0 1 1 0 .. -33dB -19dB -6dB -33dB .. -91dB
	{0x0009, -90},         //   7 .. 0 0 1 1 .. -33dB -24dB -6dB -27dB .. -90dB
	{0x0101, -89},         //   8 .. 1 0 0 1 .. -30dB -24dB -8dB -27dB .. -89dB
	{0x0030, -88},         //   9 .. 0 1 2 0 .. -33dB -19dB -3dB -33dB .. -88dB
	{0x0118, -87},         //  10 .. 1 0 3 0 .. -30dB -24dB  0dB -33dB .. -87dB
	{0x0002, -86},         //  11 .. 0 0 0 2 .. -33dB -24dB -8dB -21dB .. -86dB
	{0x0130, -85},         //  12 .. 1 1 2 0 .. -30dB -19dB -3dB -33dB .. -85dB
	{0x0019, -84},         //  13 .. 0 0 3 1 .. -33dB -24dB  0dB -27dB .. -84dB
	{0x0060, -83},         //  14 .. 0 3 0 0 .. -33dB  -9dB -8dB -33dB .. -83dB
	{0x0138, -82},         //  15 .. 1 1 3 0 .. -30dB -19dB  0dB -33dB .. -82dB
	{0x0119, -81},         //  16 .. 1 0 3 1 .. -30dB -24dB  0dB -27dB .. -81dB
	{0x0058, -80},         //  17 .. 0 2 3 0 .. -33dB -14dB  0dB -33dB .. -80dB
	{0x0141, -79},         //  18 .. 1 2 0 1 .. -30dB -14dB -8dB -27dB .. -79dB
	{0x0070, -78},         //  19 .. 0 3 2 0 .. -33dB  -9dB -3dB -33dB .. -78dB
	{0x0180, -77},         //  20 .. 1 4 0 0 .. -30dB  -6dB -8dB -33dB .. -77dB
	{0x0139, -76},         //  21 .. 1 1 3 1 .. -30dB -19dB  0dB -27dB .. -76dB
	{0x0013, -75},         //  22 .. 0 0 2 3 .. -33dB -24dB -3dB -15dB .. -75dB
	{0x0161, -74},         //  23 .. 1 3 0 1 .. -30dB  -9dB -8dB -27dB .. -74dB
	{0x01C0, -73},         //  24 .. 1 6 0 0 .. -30dB  -2dB -8dB -33dB .. -73dB
	{0x00E8, -72},         //  25 .. 0 7 1 0 .. -33dB   0dB -6dB -33dB .. -72dB
	{0x00D0, -71},         //  26 .. 0 6 2 0 .. -33dB  -2dB -3dB -33dB .. -71dB
	{0x0239, -70},         //  27 .. 2 1 3 1 .. -24dB -19dB  0dB -27dB .. -70dB
	{0x006A, -69},         //  28 .. 0 3 1 2 .. -33dB  -9dB -6dB -21dB .. -69dB
	{0x0006, -68},         //  29 .. 0 0 0 6 .. -33dB -24dB -8dB  -3dB .. -68dB
	{0x00B1, -67},         //  30 .. 0 5 2 1 .. -33dB  -4dB -3dB -27dB .. -67dB
	{0x000E, -66},         //  31 .. 0 0 1 6 .. -33dB -24dB -6dB  -3dB .. -66dB
	{0x015A, -65},         //  32 .. 1 2 3 2 .. -30dB -14dB  0dB -21dB .. -65dB
	{0x022B, -64},         //  33 .. 2 1 1 3 .. -24dB -19dB -6dB -15dB .. -64dB
	{0x01F8, -63},         //  34 .. 1 7 3 0 .. -30dB   0dB  0dB -33dB .. -63dB
	{0x0163, -62},         //  35 .. 1 3 0 3 .. -30dB  -9dB -8dB -15dB .. -62dB
	{0x0035, -61},         //  36 .. 0 1 2 5 .. -33dB -19dB -3dB  -6dB .. -61dB
	{0x0214, -60},         //  37 .. 2 0 2 4 .. -24dB -24dB -3dB  -9dB .. -60dB
	{0x01D9, -59},         //  38 .. 1 6 3 1 .. -30dB  -2dB  0dB -27dB .. -59dB
	{0x0145, -58},         //  39 .. 1 2 0 5 .. -30dB -14dB -8dB  -6dB .. -58dB
	{0x02A2, -57},         //  40 .. 2 5 0 2 .. -24dB  -4dB -8dB -21dB .. -57dB
	{0x02D1, -56},         //  41 .. 2 6 2 1 .. -24dB  -2dB -3dB -27dB .. -56dB
	{0x00B3, -55},         //  42 .. 0 5 2 3 .. -33dB  -4dB -3dB -15dB .. -55dB
	{0x0216, -54},         //  43 .. 2 0 2 6 .. -24dB -24dB -3dB  -3dB .. -54dB
	{0x0066, -53},         //  44 .. 0 3 0 6 .. -33dB  -9dB -8dB  -3dB .. -53dB
	{0x00C4, -52},         //  45 .. 0 6 0 4 .. -33dB  -2dB -8dB  -9dB .. -52dB
	{0x006E, -51},         //  46 .. 0 3 1 6 .. -33dB  -9dB -6dB  -3dB .. -51dB
	{0x015D, -50},         //  47 .. 1 2 3 5 .. -30dB -14dB  0dB  -6dB .. -50dB
	{0x00AD, -49},         //  48 .. 0 5 1 5 .. -33dB  -4dB -6dB  -6dB .. -49dB
	{0x007D, -48},         //  49 .. 0 3 3 5 .. -33dB  -9dB  0dB  -6dB .. -48dB
	{0x00D4, -47},         //  50 .. 0 6 2 4 .. -33dB  -2dB -3dB  -9dB .. -47dB
	{0x01B4, -46},         //  51 .. 1 5 2 4 .. -30dB  -4dB -3dB  -9dB .. -46dB
	{0x030B, -45},         //  52 .. 3 0 1 3 ..   0dB -24dB -6dB -15dB .. -45dB
	{0x00CE, -44},         //  53 .. 0 6 1 6 .. -33dB  -2dB -6dB  -3dB .. -44dB
	{0x01B5, -43},         //  54 .. 1 5 2 5 .. -30dB  -4dB -3dB  -6dB .. -43dB
	{0x0097, -42},         //  55 .. 0 4 2 7 .. -33dB  -6dB -3dB   0dB .. -42dB
	{0x0257, -41},         //  56 .. 2 2 2 7 .. -24dB -14dB -3dB   0dB .. -41dB
	{0x02B4, -40},         //  57 .. 2 5 2 4 .. -24dB  -4dB -3dB  -9dB .. -40dB
	{0x027D, -39},         //  58 .. 2 3 3 5 .. -24dB  -9dB  0dB  -6dB .. -39dB
	{0x01DD, -38},         //  59 .. 1 6 3 5 .. -30dB  -2dB  0dB  -6dB .. -38dB
	{0x02AE, -37},         //  60 .. 2 5 1 6 .. -24dB  -4dB -6dB  -3dB .. -37dB
	{0x0379, -36},         //  61 .. 3 3 3 1 ..   0dB  -9dB  0dB -27dB .. -36dB
	{0x035A, -35},         //  62 .. 3 2 3 2 ..   0dB -14dB  0dB -21dB .. -35dB
	{0x02B6, -34},         //  63 .. 2 5 2 6 .. -24dB  -4dB -3dB  -3dB .. -34dB
	{0x030E, -33},         //  64 .. 3 0 1 6 ..   0dB -24dB -6dB  -3dB .. -33dB
	{0x0307, -32},         //  65 .. 3 0 0 7 ..   0dB -24dB -8dB   0dB .. -32dB
	{0x02BE, -31},         //  66 .. 2 5 3 6 .. -24dB  -4dB  0dB  -3dB .. -31dB
	{0x037A, -30},         //  67 .. 3 3 3 2 ..   0dB  -9dB  0dB -21dB .. -30dB
	{0x02DE, -29},         //  68 .. 2 6 3 6 .. -24dB  -2dB  0dB  -3dB .. -29dB
	{0x0345, -28},         //  69 .. 3 2 0 5 ..   0dB -14dB -8dB  -6dB .. -28dB
	{0x03A3, -27},         //  70 .. 3 5 0 3 ..   0dB  -4dB -8dB -15dB .. -27dB
	{0x0364, -26},         //  71 .. 3 3 0 4 ..   0dB  -9dB -8dB  -9dB .. -26dB
	{0x032F, -25},         //  72 .. 3 1 1 7 ..   0dB -19dB -6dB   0dB .. -25dB
	{0x0393, -24},         //  73 .. 3 4 2 3 ..   0dB  -6dB -3dB -15dB .. -24dB
	{0x0384, -23},         //  74 .. 3 4 0 4 ..   0dB  -6dB -8dB  -9dB .. -23dB
	{0x0347, -22},         //  75 .. 3 2 0 7 ..   0dB -14dB -8dB   0dB .. -22dB
	{0x03EB, -21},         //  76 .. 3 7 1 3 ..   0dB   0dB -6dB -15dB .. -21dB
	{0x03D3, -20},         //  77 .. 3 6 2 3 ..   0dB  -2dB -3dB -15dB .. -20dB
	{0x03BB, -19},         //  78 .. 3 5 3 3 ..   0dB  -4dB  0dB -15dB .. -19dB
	{0x037C, -18},         //  79 .. 3 3 3 4 ..   0dB  -9dB  0dB  -9dB .. -18dB
	{0x03CC, -17},         //  80 .. 3 6 1 4 ..   0dB  -2dB -6dB  -9dB .. -17dB
	{0x03C5, -16},         //  81 .. 3 6 0 5 ..   0dB  -2dB -8dB  -6dB .. -16dB
	{0x03EC, -15},         //  82 .. 3 7 1 4 ..   0dB   0dB -6dB  -9dB .. -15dB
	{0x035F, -14},         //  83 .. 3 2 3 7 ..   0dB -14dB  0dB   0dB .. -14dB
	{0x03BC, -13},         //  84 .. 3 5 3 4 ..   0dB  -4dB  0dB  -9dB .. -13dB
	{0x038F, -12},         //  85 .. 3 4 1 7 ..   0dB  -6dB -6dB   0dB .. -12dB
	{0x03E6, -11},         //  86 .. 3 7 0 6 ..   0dB   0dB -8dB  -3dB .. -11dB
	{0x03AF, -10},         //  87 .. 3 5 1 7 ..   0dB  -4dB -6dB   0dB .. -10dB
	{0x03F5,  -9},         //  88 .. 3 7 2 5 ..   0dB   0dB -3dB  -6dB ..  -9dB
	{0x03D6,  -8},         //  89 .. 3 6 2 6 ..   0dB  -2dB -3dB  -3dB ..  -8dB
	{0x03BE,  -7},         //  90 .. 3 5 3 6 ..   0dB  -4dB  0dB  -3dB ..  -7dB original
	{0x03F6,  -6},         //  91 .. 3 7 2 6 ..   0dB   0dB -3dB  -3dB ..  -6dB
	{0x03DE,  -5},         //  92 .. 3 6 3 6 ..   0dB  -2dB  0dB  -3dB ..  -5dB
	{0x03BF,  -4},         //  93 .. 3 5 3 7 ..   0dB  -4dB  0dB   0dB ..  -4dB
	{0x03F7,  -3},         //  94 .. 3 7 2 7 ..   0dB   0dB -3dB   0dB ..  -3dB
	{0x03DF,  -2},         //  95 .. 3 6 3 7 ..   0dB  -2dB  0dB   0dB ..  -2dB
	{0x03FF,   0},         //  96 .. 3 7 3 7 ..   0dB   0dB  0dB   0dB ..   0dB
};
#endif

static const t_gain_table gain_table[] =
{
	{ (3 << 8)|(0 << 5)|(0 << 3)|(0 << 0), -65 },  // index:  0, LNAS 0dBm, LNA -24dBm, MIX -8dBm, PGA -33dBm --> -65dBm total
	{ (3 << 8)|(0 << 5)|(1 << 3)|(0 << 0), -63 },  // index:  1, LNAS 0dBm, LNA -24dBm, MIX -6dBm, PGA -33dBm --> -63dBm total
	{ (3 << 8)|(0 << 5)|(2 << 3)|(0 << 0), -60 },  // index:  2, LNAS 0dBm, LNA -24dBm, MIX -3dBm, PGA -33dBm --> -60dBm total
	{ (3 << 8)|(0 << 5)|(0 << 3)|(1 << 0), -59 },  // index:  3, LNAS 0dBm, LNA -24dBm, MIX -8dBm, PGA -27dBm --> -59dBm total
	{ (3 << 8)|(0 << 5)|(1 << 3)|(1 << 0), -57 },  // index:  4, LNAS 0dBm, LNA -24dBm, MIX -6dBm, PGA -27dBm --> -57dBm total
	{ (3 << 8)|(0 << 5)|(2 << 3)|(1 << 0), -54 },  // index:  5, LNAS 0dBm, LNA -24dBm, MIX -3dBm, PGA -27dBm --> -54dBm total
	{ (3 << 8)|(1 << 5)|(3 << 3)|(0 << 0), -52 },  // index:  6, LNAS 0dBm, LNA -19dBm, MIX  0dBm, PGA -33dBm --> -52dBm total
	{ (3 << 8)|(0 << 5)|(3 << 3)|(1 << 0), -51 },  // index:  7, LNAS 0dBm, LNA -24dBm, MIX  0dBm, PGA -27dBm --> -51dBm total
	{ (3 << 8)|(1 << 5)|(2 << 3)|(1 << 0), -49 },  // index:  8, LNAS 0dBm, LNA -19dBm, MIX -3dBm, PGA -27dBm --> -49dBm total
	{ (3 << 8)|(0 << 5)|(2 << 3)|(2 << 0), -48 },  // index:  9, LNAS 0dBm, LNA -24dBm, MIX -3dBm, PGA -21dBm --> -48dBm total
	{ (3 << 8)|(1 << 5)|(1 << 3)|(2 << 0), -46 },  // index: 10, LNAS 0dBm, LNA -19dBm, MIX -6dBm, PGA -21dBm --> -46dBm total
	{ (3 << 8)|(1 << 5)|(2 << 3)|(2 << 0), -43 },  // index: 11, LNAS 0dBm, LNA -19dBm, MIX -3dBm, PGA -21dBm --> -43dBm total
	{ (3 << 8)|(1 << 5)|(3 << 3)|(2 << 0), -40 },  // index: 12, LNAS 0dBm, LNA -19dBm, MIX  0dBm, PGA -21dBm --> -40dBm total
	{ (3 << 8)|(2 << 5)|(2 << 3)|(2 << 0), -38 },  // index: 13, LNAS 0dBm, LNA -14dBm, MIX -3dBm, PGA -21dBm --> -38dBm total
	{ (3 << 8)|(2 << 5)|(3 << 3)|(2 << 0), -35 },  // index: 14, LNAS 0dBm, LNA -14dBm, MIX  0dBm, PGA -21dBm --> -35dBm total
	{ (3 << 8)|(2 << 5)|(2 << 3)|(3 << 0), -32 },  // index: 15, LNAS 0dBm, LNA -14dBm, MIX -3dBm, PGA -15dBm --> -32dBm total
	{ (3 << 8)|(2 << 5)|(3 << 3)|(3 << 0), -29 },  // index: 16, LNAS 0dBm, LNA -14dBm, MIX  0dBm, PGA -15dBm --> -29dBm total
	{ (3 << 8)|(0 << 5)|(3 << 3)|(6 << 0), -27 },  // index: 17, LNAS 0dBm, LNA -24dBm, MIX  0dBm, PGA  -3dBm --> -27dBm total
	{ (3 << 8)|(2 << 5)|(2 << 3)|(4 << 0), -26 },  // index: 18, LNAS 0dBm, LNA -14dBm, MIX -3dBm, PGA  -9dBm --> -26dBm total
	{ (3 << 8)|(1 << 5)|(2 << 3)|(6 << 0), -25 },  // index: 19, LNAS 0dBm, LNA -19dBm, MIX -3dBm, PGA  -3dBm --> -25dBm total
	{ (3 << 8)|(2 << 5)|(3 << 3)|(4 << 0), -23 },  // index: 20, LNAS 0dBm, LNA -14dBm, MIX  0dBm, PGA  -9dBm --> -23dBm total
	{ (3 << 8)|(5 << 5)|(0 << 3)|(4 << 0), -21 },  // index: 21, LNAS 0dBm, LNA  -4dBm, MIX -8dBm, PGA  -9dBm --> -21dBm total
	{ (3 << 8)|(2 << 5)|(2 << 3)|(6 << 0), -20 },  // index: 22, LNAS 0dBm, LNA -14dBm, MIX -3dBm, PGA  -3dBm --> -20dBm total
	{ (3 << 8)|(5 << 5)|(1 << 3)|(4 << 0), -19 },  // index: 23, LNAS 0dBm, LNA - 4dBm, MIX -6dBm, PGA  -9dBm --> -19dBm total
	{ (3 << 8)|(3 << 5)|(1 << 3)|(6 << 0), -18 },  // index: 24, LNAS 0dBm, LNA  -9dBm, MIX -6dBm, PGA  -3dBm --> -18dBm total
	{ (3 << 8)|(2 << 5)|(2 << 3)|(7 << 0), -17 },  // index: 25, LNAS 0dBm, LNA -14dBm, MIX -3dBm, PGA   0dBm --> -17dBm total
	{ (3 << 8)|(5 << 5)|(2 << 3)|(4 << 0), -16 },  // index: 26, LNAS 0dBm, LNA  -4dBm, MIX -3dBm, PGA  -9dBm --> -16dBm total
	{ (3 << 8)|(3 << 5)|(1 << 3)|(7 << 0), -15 },  // index: 27, LNAS 0dBm, LNA  -9dBm, MIX -6dBm, PGA   0dBm --> -15dBm total
	{ (3 << 8)|(4 << 5)|(0 << 3)|(7 << 0), -14 },  // index: 28, LNAS 0dBm, LNA  -6dBm, MIX -8dBm, PGA   0dBm --> -14dBm total
	{ (3 << 8)|(5 << 5)|(2 << 3)|(5 << 0), -13 },  // index: 29, LNAS 0dBm, LNA  -4dBm, MIX -3dBm, PGA  -6dBm --> -13dBm total
	{ (3 << 8)|(3 << 5)|(2 << 3)|(7 << 0), -12 },  // index: 30, LNAS 0dBm, LNA  -9dBm, MIX -3dBm, PGA   0dBm --> -12dBm total
	{ (3 << 8)|(5 << 5)|(3 << 3)|(5 << 0), -10 },  // index: 31, LNAS 0dBm, LNA  -4dBm, MIX  0dBm, PGA  -6dBm --> -10dBm total
	{ (3 << 8)|(4 << 5)|(3 << 3)|(6 << 0),  -9 },  // index: 32, LNAS 0dBm, LNA  -6dBm, MIX  0dBm, PGA  -3dBm -->  -9dBm total
	{ (3 << 8)|(5 << 5)|(3 << 3)|(6 << 0),  -7 },  // index: 33, LNAS 0dBm, LNA  -4dBm, MIX  0dBm, PGA  -3dBm -->  -7dBm total
	{ (3 << 8)|(4 << 5)|(3 << 3)|(7 << 0),  -6 },  // index: 34, LNAS 0dBm, LNA  -6dBm, MIX  0dBm, PGA   0dBm -->  -6dBm total
	{ (3 << 8)|(6 << 5)|(3 << 3)|(6 << 0),  -5 },  // index: 35, LNAS 0dBm, LNA  -2dBm, MIX  0dBm, PGA  -3dBm -->  -5dBm total
	{ (3 << 8)|(5 << 5)|(3 << 3)|(7 << 0),  -4 },  // index: 36, LNAS 0dBm, LNA  -4dBm, MIX  0dBm, PGA   0dBm -->  -4dBm total
	{ (3 << 8)|(6 << 5)|(3 << 3)|(7 << 0),  -2 },  // index: 37, LNAS 0dBm, LNA  -2dBm, MIX  0dBm, PGA   0dBm -->  -2dBm total
	{ (3 << 8)|(7 << 5)|(3 << 3)|(7 << 0),   0 },  // index: 38, LNAS 0dBm, LNA   0dBm, MIX  0dBm, PGA   0dBm -->   0dBm total
};

uint8_t gAmFixStandbyIndex = ARRAY_SIZE(gain_table) + STANDBY_INDEX_RELATIVE_TO_MAX;
static const unsigned int original_index = ARRAY_SIZE(gain_table) - 7;

unsigned int gain_table_index[2] = {original_index, original_index};

// used simply to detect a changed gain setting
unsigned int gain_table_index_prev[2] = {0, 0};

// holds the previous RSSI level .. we do an average of old + new RSSI reading
int16_t prev_rssi[2] = {0, 0};

// to help reduce gain hunting, peak hold count down tick
uint8_t hold_counter[2] = {0, 0};

// used to correct the RSSI readings after our RF gain adjustments
int16_t rssi_gain_diff[2] = {0, 0};

// used to limit the max RF gain
unsigned int max_index = ARRAY_SIZE(gain_table) - 1;

// -89 dBm, any higher and the AM demodulator starts to saturate/clip/distort
const int16_t desired_rssi = (CAP_DBM + 160) * 2;

void AM_fix_init(void)
{	// called at boot-up

	unsigned int i;

	for (i = 0; i < 2; i++)
	{
		gain_table_index[i] = original_index;  // re-start with original QS setting
	}
}

void AM_fix_reset(const int vfo)
{	// reset the AM fixer upper

	prev_rssi[vfo] = 0;

	hold_counter[vfo] = 0;

	rssi_gain_diff[vfo] = 0;

	gain_table_index_prev[vfo] = 0;
}

// adjust the RX gain to try and prevent the AM demodulator from
// saturating/overloading/clipping (distorted AM audio)
//
// we're actually doing the BK4819's job for it here, but as the chip
// won't/don't do it for itself, we're left to bodging it ourself by
// playing with the RF front end gain setting
//
void Task_AM_fix()
{
	if(gAmFixCountdown != 0 || !gExtendedSettings.AmFixEnabled) {
		//if(!bFgcSet) {
		//	BK4819_ForceFGCMode(1);
		//	bFgcSet = true;
		//}
		return;
	}

	if(gVfoState[gSettings.CurrentVfo].gModulationType == 1) {	// AM
		int16_t diff_dB;
		int16_t rssi;
		const int vfo = gCurrentVfo;

		switch (gRadioMode) {
				//case RADIO_MODE_QUIET:
				case RADIO_MODE_TX:
				gAmFixCountdown = 50; // 100
				return;

			// only adjust stuff if we're in one of these modes
			case RADIO_MODE_QUIET:
			case RADIO_MODE_INCOMING:
			case RADIO_MODE_RX:
				break;
		}

		{	// sample the current RSSI level
			// average it with the previous rssi (a bit of noise/spike immunity)
			const int16_t new_rssi = BK4819_GetRSSI();
			rssi                   = (prev_rssi[vfo] > 0) ? (prev_rssi[vfo] + new_rssi) / 2 : new_rssi;
			prev_rssi[vfo]         = new_rssi;
		}

		// automatically adjust the RF RX gain

		// update the gain hold counter
		if (hold_counter[vfo] > 0) {
			hold_counter[vfo]--;
		}

		// dB difference between actual and desired RSSI level
		diff_dB = (rssi - desired_rssi) / 2;

		if (diff_dB > 0) {
			// decrease gain
			//gpio_bits_set(GPIOA, BOARD_GPIOA_LED_RED);

			gAmFixIndex = gain_table_index[vfo];   // current position we're at
			//unsigned int index = gain_table_index[vfo];   // current position we're at

			if (diff_dB >= 6) {
				// jump immediately to a new gain setting
				// this greatly speeds up initial gain reduction (but reduces noise/spike immunity)

				const int16_t desired_gain_dB = (int16_t)gain_table[gAmFixIndex].gain_dB - diff_dB + 6; //8; // get no closer than 8dB (bit of noise/spike immunity)

				// scan the table to see what index to jump straight too
				while (gAmFixIndex > 1)
					if (gain_table[--gAmFixIndex].gain_dB <= desired_gain_dB)
						break;

				//gAmFixIndex = (gain_table_index[vfo] + gAmFixIndex) / 2;  // easy does it
			} else {	// incrementally reduce the gain .. taking it slow improves noise/spike immunity

				if (gAmFixIndex >= (1 + 3) && diff_dB >= 3)
					gAmFixIndex -= 3;  // faster gain reduction
				else
				if (gAmFixIndex > 1) {
					gAmFixIndex--;     // slow step-by-step gain reduction
				}
			}

			gAmFixIndex = (gAmFixIndex < 1) ? 1 : (gAmFixIndex > (ARRAY_SIZE(gain_table) - 1)) ? ARRAY_SIZE(gain_table) - 1 : gAmFixIndex;

			if (gain_table_index[vfo] != gAmFixIndex) {
				gain_table_index[vfo] = gAmFixIndex;
				hold_counter[vfo] = 30;       // 300ms hold
			}
		}

		if (diff_dB >= -3) {                   // 6dB hysterisis (help reduce gain hunting)
			hold_counter[vfo] = 30;           // 300ms hold
		}

		if (hold_counter[vfo] == 0) {
			// hold has been released, we're free to increase gain
			//gpio_bits_reset(GPIOA, BOARD_GPIOA_LED_RED);
			gAmFixIndex = gain_table_index[vfo] + 1;                 // move up to next gain index
			//gain_table_index[vfo] = (index < ARRAY_SIZE(gain_table)) ? index : ARRAY_SIZE(gain_table) - 1;     // limit the gain index
			gain_table_index[vfo] = (gAmFixIndex < gAmFixStandbyIndex) ? gAmFixIndex : gAmFixStandbyIndex;     // limit the gain index to 90 (original QS)
		}

		{	// apply the new settings to the front end registers
			gAmFixIndex = gain_table_index[vfo];

			// remember the new table index
			gain_table_index_prev[vfo] = gAmFixIndex;

			BK4819_WriteRegister(0x13, gain_table[gAmFixIndex].reg_val);

			// offset the RSSI reading to the rest of the firmware to cancel out the gain adjustments we make

			// RF gain difference from original QS setting
			rssi_gain_diff[vfo] = ((int16_t)gain_table[gAmFixIndex].gain_dB - gain_table[original_index].gain_dB) * 2;
		}

		gAmFixCountdown = 30;
	} else {
		gAmFixCountdown = 200;
		BK4819_RestoreGainSettings();
	}
}

#endif
