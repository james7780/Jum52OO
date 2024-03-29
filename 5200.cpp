//
//  Jum's A5200 Emulator
//
//  Cross-platform codebase, version 1.3
//
//  Copyright James Higgs 1999-2016
//
//====================================================================
//  5200 emu main prog
//====================================================================

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include "global.h"
#include "5200.h"
#include "host.h"

#include "rom.h"

#ifdef WIN32
#include <Windows.h>
#endif

// Our global C5200 instance;
extern C5200 jum52;

// Write debug output strings
void DebugPrint(const char* format, ...)
{
		char str[1024];
		va_list argptr;
		va_start(argptr, format);
		vsnprintf(str, sizeof(str), format, argptr);
		va_end(argptr);

#ifdef WIN32
    OutputDebugStringA(str);
#else
		printf(str);
#endif
}

// TODO: PM fix in narrow mode
// TODO: fix collision detection

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

//#define WHITE		15
//#define DARK_GREY	2
//#define GREY		6
//#define DX			20
//#define DY			20

#define KEYS_LINE		245
#define VBI_LINE		248

// global variables
const char* emu_version = "Jum's 5200 Emulator V" JUM52_VERSION;
//CONTROLLER controller1, controller2;
char errormsg[256];
int numSampleEvents[4] = { 0, 0, 0, 0 };
static SampleEvent_t sampleEvent[4][MAX_SAMPLE_EVENTS];

Options_t options = {			// emulator options (see global.h)
				NTSC,							// videomode: PAL or NTSC
				0,								// debug: 0 (debug mode) or 1 (normal mode)
				0,								// controller: 0 (keys), 1 (joystick), 2 (mouse)
				0,								// controlmode: 0 (normal), 1 (robotron), 2 (pengo)
				1,								// audio: 0 (off) or 1 (on)
				1,								// voice: 0 (off) or 1 (on)
				75,								// volume: 0 to 100 %
				0,								// 0 (windowed) or 1 (fullscreen)
				1,								// scale: 1, 2, 3, 4
				0,								// scanlines off = 0 (1 = 25%, 2 = 50%. 3 = 75%)
				0									// slow: 0 (off) or 1 (on)
		};
uint16 snd_buf_size = 0;        // different for NTSC or PAL, and different sample rates
uint8* snd = NULL;
uint8* memory5200 = NULL;				// pointer to 6502 memory
unsigned short keyMap[KEY_MAP_SIZE];	// for keyboard remapping (includes "extended" keycodes up to MENU key)

// WHy do we have both of these?
#define CYCLESPERLINE	120 //114
#define TICKSTOHBL	136

// local variables
static int isEmulating = 0;
static char logmsg[256];
static int8* voiceBuffer = NULL;
static uint8 currVoiceVal[4] = { 0, 0, 0, 0 };			// value carried over from previous frame
//uint8 keypad[10];
//uint8 kbcode, kbcode2;						// joy 1 and 2 keyboard
static uint8* mapper = NULL;				// address to peripheral mapping
//static uint8 trig[4];
static Map16k_t* p16kMaps = NULL;		// pointer to 16k rom mappings
static int num16kMappings = 0;			// number of 16k rom mappings

/* MEMORY MAP										INDEX  (0 is invalid)
*	0-3FFF		RAM				(read/write)		1
*	4000-BFFF	ROM				(read only)			2
*	C000-C0FF	GTIA regs								3
*   D300-D3FF   Serial???                           7
*	D400-D4FF	ANTIC regs								4
*	E000		I/O expansion								5
*	E800-E8FF	POKEY regs								6
*	EB00-EBFF	also POKEY regs???					6
*	F800-FFFF	ROM	(BIOS)			(read only)			2
*/
#define MAPPER_INVALID  0
#define MAPPER_RAM      1
#define MAPPER_ROM      2
#define MAPPER_GTIA     3
#define MAPPER_ANTIC    4
#define MAPPER_IOEXP    5
#define MAPPER_POKEY    6

//////////////////////////////////////////////////////////////////////////////
// Controller - represents player's controller state
//////////////////////////////////////////////////////////////////////////////
CController::CController()
{
	Initialise();
}

// Initialise controller
void CController::Initialise()
{
	mode = CONT_MODE_DIGITAL;					// digital
	left = 0;
	right = 0;
	up = 0;
	down = 0;
	analog_h = 0;
	analog_v = 0;
	trig = 0;
	side_button = 0;
	keys[16] = ( 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 );
	last_key_still_pressed = 0;
	// Mode
	fake_mouse_support = 0;
	// private
	vpos = 0;
	hpos = 0;
	lastRead = 0;;
}


//////////////////////////////////////////////////////////////////////////////
// CANTIC - Atari 5200 ANTIC chip functionality
//////////////////////////////////////////////////////////////////////////////
CANTIC::CANTIC()
{
	m_vcount = -2;
	m_ticksToHSYNC = CYCLESPERLINE;				// should be 114
	m_ticksTillDraw = 9999999;
	m_dladdr = 0;
	m_scanaddr = 0;
	m_currentLineMode = 0;		// running current ANTIC line mode
	m_nextDLI = 300;
	m_nextModeLine = 0;
	m_finished = 0;
	m_playfieldLineCount = 0;
	m_vscroll = FALSE;
	m_hscroll = FALSE;
	m_vscrollStartLine = 0;

	m_nmien = 0;
	m_nmist = 0;
}

// initialise ANTIC parameters
void CANTIC::Initialise()
{
	m_vcount = 0;
	//scanline = 0;
	//scrolltrig = 0;
	//vscrollStartLine = 0;
	//bytes_per_line = 0;
	//framesdrawn = 0;

	//stolencycles = 0;
	m_dladdr = 0;
	m_scanaddr = 0;
	m_currentLineMode = 0;		// running current ANTIC line mode
	m_nextDLI = 300;
	m_nextModeLine = 0;
	m_finished = 0; //display list finished?
	m_playfieldLineCount = 0;
	m_vscroll = FALSE;
	m_hscroll = FALSE;

	m_nmien = 0;
	m_nmist = 0;
}

// Read from ANTIC regs
uint8 CANTIC::Read(uint16 addr)
{
		uint8 ANTICreg = addr & 0xf;
		switch (ANTICreg) {
		case 0x00:	// DMACTL
		case 0x01:	// CHACTL
		case 0x02:	// DLISTL
		case 0x03:	// DLISTH
		case 0x04:	// HSCROL
		case 0x05:	// VSCROL
				return 0xFF;
				break;
		case 0x06:	// unused?
#ifdef DEBUG
			fprintf(stderr, "ANTIC read of unused (reg 6)\n");
#endif
			return 0xFF;
				break;
		case 0x07:	// PMBASE
				return 0xFF;
				break;
		case 0x08:	// unused?
#ifdef DEBUG
			fprintf(stderr, "ANTIC read of unused (reg 8)\n");
#endif
			return 0xFF;
				break;
		case 0x09:	// CHBASE
				return 0xFF;
				break;
		case 0x0A:	// WSYNC
				return 0xFF;
				break;
		case 0x0B:	// VCOUNT
				// Note: some games require accurate VCOUNT values
				//       I don't have it 100% accurate yet
				// return upper 8 bits of 9-bit counter
				// (ie: scanline / 2)
				//return (vcount / 2 + 4);              // gets Star Raiders working!
				if (m_vcount < 0) return 0;
				else
						return (m_vcount >> 1) & 0xFF;            // JH 15/3/2002
				break;
		case 0x0C:	// PENH
				return 0x80;				// hack
				break;
		case 0x0D:	// PENV
				return 0x80;				// hack
				break;
		case 0x0E:	//	NMIEN (write-only)
				return 0xFF;
				break;
		case 0x0F:	//	NMIST
				return m_nmist;
				break;
		}

		// Oops!
		return 0;
}

// Write to ANTIC regs
void CANTIC::Write(uint16 addr, uint8 byte)
{
		switch (addr) {
		case 0xD400:	//	DMACTL
				memory5200[DMACTL] = byte;
				break;
		case 0xD401:	//	CHACTL
				memory5200[CHACTL] = byte;
				break;
		case 0xD402:	//	DLISTL
				memory5200[DLISTL] = byte;
				break;
		case 0xD403:	//	DLISTH
				memory5200[DLISTH] = byte;
				break;
		case 0xD404:	//	HSCROL
				memory5200[HSCROL] = byte;
				break;
		case 0xD405:	//	VSCROL
				memory5200[VSCROL] = byte;
				break;
		case 0xD406:	//  UNKNOWN
				break;
		case 0xD407:	//	PMBASE
				memory5200[PMBASE] = byte;
				break;
		case 0xD408:	//  UNKNOWN
				break;
		case 0xD409:	//  CHBASE
				memory5200[CHBASE] = byte;
				break;
		case 0xD40A:	//  WSYNC
				WSync();
				break;
		case 0xD40B:	//  VCOUNT
				break;
		case 0xD40C:	//  PENH
				break;
		case 0xD40D:	//  PENV
				break;
		case 0xD40E:	//	NMIEN
				m_nmien = byte;
				memory5200[NMIEN] = byte;
				break;
		case 0xD40F:	//	NMIRES
				m_nmist = 0x0;
				memory5200[NMIRES] = 0x0;
				break;
		}
}

// Do ANTIC horizontal wait sync
void CANTIC::WSync()
{
	// writing to WSYNC halts CPU until 7 machine cycles
	// (14 color clocks) before next scan line, to prevent
	// changes to current scan line from DLI routine
	// In an emulator, we just 'count' off the cycles
	// remaining in this scanline.
	// NB: modified to keep updating ANTIC 10x4 cycles
	// (otherwise line draw may be missed!)
	// tris = ticks remaining in scanline
	// OPTIMISE!
	// JH 28/1/2002: timing info:
	//      Wide PF: HBLANK = 18 machine cycles
	//      Normal PF: HBLANK = 34 machine cycles
	//      Narrow PF: HBLANK = 50 machine cycles
	/*
	while(ticksToHSYNC > 10) {
	updateANTIC(4);
	}
	*/
	if (m_ticksToHSYNC < 10) Update();			//was C5200::updateANTIC();
	m_ticksToHSYNC = 7;
	if (m_ticksTillDraw < 100) m_ticksTillDraw = -1;
}

// update ANTIC and check if time for VBI
void CANTIC::Update()
{
	m_vcount++;

	// DEBUG
	//fprintf(stderr, "vcount=%d\n", vcount);

	// Reset VBI status bit on line 39
	if (39 == m_vcount)
		{
		// reset VBI NMI interrupt
		m_nmist &= 0xBF;
		memory5200[NMIST] = m_nmist;
		}

	//// check for key IRQ just before VBI
	//// (otherwise it gets lost in VBI)
	//// (do_keys() commented out in code below)
	//if(vcount == KEYS_LINE || vcount == 120) {
	//		do_keys();				// get kbcode and set interrupt
	//}	

	// NB: 5200 will do a keyboard IRQ every 32 scanlines if a key is held down
	//     (and kbd IRQ is enabled)
	if (32 == m_vcount || 64 == m_vcount || 96 == m_vcount || 128 == m_vcount || 160 == m_vcount || 192 == m_vcount)
		{
		jum52.do_keys();				// get kbcode and trigger interrupt if neccessary
		}

	// do actual drawing area of screen
	if (m_vcount <= VBI_LINE) {
		// do DLI NMI if we are on the last line of a DL instr
		// with a DLI enabled
		// NB: this is sensitive!!!
		if (m_vcount == m_nextDLI) {
			m_nmist |= 0x80;
			memory5200[NMIST] |= 0x80;
			if (m_nmien & 0x80) {
				jum52.CPU.DoNMI();		//nmi6502();
			}
		}

		// SCHEDULE DRAW SCANLINE for 40 CTIA cycles time
		// 20 CPU cycles
		m_ticksTillDraw = 20;

		// do VBI if we are at line 248
		if ( VBI_LINE == m_vcount) {
			// update sound
			//Pokey_process(snd, SND_BUF_SIZE);
			HostProcessSoundBuffer();

			// set off VBI by setting VBI status bit
			// and calling NMI (if enabled)
			m_nmist = 0x40;			// HACK
			memory5200[NMIST] = 0x40;		// HACK

			// do VBI NMI if VBI enabled in NMIEN
			// NOTE: to get diagnostic cart working,
			// enable VBI NMI after x cycles
			if (m_nmien & 0x40) {
				jum52.CPU.DoNMI();				//nmi6502();
			}

			// Do the video copy. Speed throttle goes here
			// Or in the HostProcessSoundBuffer routine.
			HostBlitVideo();
			// clear collision buffer
			memset(jum52.m_collbuff, 0, VID_WIDTH * VID_HEIGHT);


			m_finished = FALSE;
			jum52.m_framesdrawn++;
			m_playfieldLineCount = 0;

			// Controller/Event processing
			HostDoEvents();

			// RFB: What's this for?
			// JH: Joystick B button used for A5200 controller
			// "side buttons", which cause a BRK interrupt.
			//if(joy[0].button[1].b)
			//{
			//	m_irqst &= 0x7F;	// BRK key
			//	// check irqen and do interrupt if bit 7 set
			//	if(irqen & 0x80) irq6502();
			//}

		} // end if VBI
	} // end if vcount < 249
	else {
		// just check for end of frame
		// fiddling with this might get some games working???
		if (260 == m_vcount) {
			m_vcount = -2;
			m_nextModeLine = 8;
			//scanline = 0;
			m_nextDLI = 300;			// disable next dli until next frame
			// reload scan counter address
			m_dladdr = memory5200[DLISTL] + 256 * memory5200[DLISTH];
		}
	} // end else scanline < 249


}


//////////////////////////////////////////////////////////////////////////////
// C5200 - Atari 5200 "machine" class
//////////////////////////////////////////////////////////////////////////////
C5200::C5200()
{
		//Initialise();	// Cannot init here as mapper is NULL

		pot_max_left = POT_LEFT;
		pot_max_right = POT_RIGHT;

		//global5200 = this;				// so 6502 can use 

		//tickstoVBI = 29829;					// not used
		//ticksToHSYNC = CYCLESPERLINE;				// should be 114
		//ticksTillDraw = 9999999;
		//vcount = -2;
		//m_nextDLI = 300;

		// Move these to ANTIC?
		//vscroll = FALSE;
		//hscroll = FALSE;
		//vscrollStartLine = 0;
		//next_mode_line = 0;
		bytes_per_line = 0;
		m_framesdrawn = 0;

		stolencycles = 0;
		//dladdr = 0;
		//finished = 0; //display list finished?
		//pfLineCount = 0;

		// init IRQ's
		m_irqst = 0xFF;                // init to no IRQ state
		m_irqen = 0x0;                // no IRQ's enabled

		m_running = 0;
}

// Fast version of Execute Instruction
//void exec6502fast(int n)			(WAS in 6502.c)
void C5200::RunFast(int n)
{
    // NB: This was moved from 6502.c as C6502 can't access ANTIC functions (updateANTIC, pm_line_render, etc)
		int timerTicks = n;
		//uint8 opcode;

		sprintf(errormsg, "Executing %d CPU ticks...\n", timerTicks);
		HostLog(errormsg);

		//#ifdef _EE
		//	printf("Executing %d CPU ticks...\n", timerTicks);
		//#endif

		antic.m_ticksToHSYNC = TICKSTOHBL;				// Initial value

		m_running = 1;
		// RFB: Don't check running every opcode!
		for (; ; ) {
				// Execute one instruction on the CPU
				int clockTicks6502 = CPU.ExecuteInstruction();

				// Update line counters
				antic.m_ticksToHSYNC -= clockTicks6502;
				antic.m_ticksTillDraw -= clockTicks6502;

				// check for VBI or horiz sync etc
				// check for HSYNC / DLI
				if(antic.m_ticksToHSYNC < 0) {
						antic.m_ticksToHSYNC += (TICKSTOHBL - stolencycles);
						antic.Update();
				}

				// check for draw scheduled
				if(antic.m_ticksTillDraw < 0) {
						// render playfield, then player/missiles
						pf_line_render();
						pm_line_render(antic.m_vcount);
						antic.m_ticksTillDraw = 99999;

						// Check if we've been stopped.
						if (!m_running)
								break;
				}
				//totalTicks += clockTicks6502;
		}
}

// Debug version of Execute Instruction
void C5200::RunDebug(int n)
{
	// TODO - Test
		int timerTicks = n;
		//uint8 opcode;
		int clockTicks6502;

		sprintf(errormsg, "Executing %d CPU ticks...\n", timerTicks);
		HostLog(errormsg);

		while (timerTicks > 0) {
				// Execute one instruction on the CPU
				clockTicks6502 = CPU.ExecuteInstruction();

				// Update line counters
				antic.m_ticksToHSYNC -= clockTicks6502;
				antic.m_ticksTillDraw -= clockTicks6502;

				// check for VBI or horiz sync etc
				// check for HSYNC / DLI
				if(antic.m_ticksToHSYNC < 0) {
						antic.m_ticksToHSYNC += (TICKSTOHBL - stolencycles);
						antic.Update();
				}

				// check for draw scheduled
				if(antic.m_ticksTillDraw < 0) {
						// render playfield, then player/missiles
						pf_line_render();
						pm_line_render(antic.m_vcount);
						antic.m_ticksTillDraw = 99999;
				}

				timerTicks -= clockTicks6502;
		}
}

// initialise GTIA regs
void C5200::initGTIA(void)
{
		trig[0] = 0x1;		// triggers off
		trig[1] = 0x1;
		trig[2] = 0x1;
		trig[3] = 0x1;
}

// check keyboard and set kbcode on VBI
void C5200::do_keys(void)
{
	// NB: 5200 will do a keyboard IRQ every 32 scanlines if a key is held down
	CController* which = NULL;

	// "loose bit" (bit 5 of KBCODE) - fluctuates 0 or 1 randomly
	uint8 loose_bit = (m_framesdrawn & 0x1) << 5;

	switch (memory5200[CONSOL] & 0x03) {
	case 0:
		which = &controller1; break;
	case 1:
		which = &controller2; break;
		// 3 and 4 in the future
	default:
		return;
	}

	// Default to "key not pressed"
	memory5200[KBCODE] = loose_bit | 0x1F;
	which->last_key_still_pressed = 0;

	for (int8 i = 0; i < 16; i++)
		{
		if (which->keys[i])
			{
			/* 2016-06-18 - commented out (reset in HostDoEvents())
						which->key[i] = 0;
			            which->last_key_still_pressed = 0;
			
						// Don't respond to the same thing twice in a row...
						if (i == which->lastRead)
						{
			                which->last_key_still_pressed = 1;      // flag key still held
							return;
						}
			*/
			if (i == which->lastRead)
				{
				which->last_key_still_pressed = 1;      // flag key still held
//				return;				// Added back 2017-05-02 to fix PAUSE in games
// 2010-10-21 - Commented out again! (prevents START on some games)
				}

			which->lastRead = i;

			// Write in the change
			//memory5200[KBCODE] = (i << 1) | loose_bit | 0x1;
			memory5200[KBCODE] = (i << 1) | 0x1;
#ifdef _DEBUG
			DebugPrint("Key press: (key %d) KBCODE = %02X\n", i, memory5200[KBCODE]);
#endif
			// set KEY interrupt bit (bit 6) to 0 (key int req "on")
			m_irqst &= 0xbf;

			// check irqen and do interrupt if bit 6 set
			if(m_irqen & 0x40)
				{
#ifdef _DEBUG
				DebugPrint("Key interrupt (key %d) IH = %04X, IHC = %04X\n", i, memory5200[0x208] + 256 * memory5200[0x209],
							memory5200[0x20A] + 256 * memory5200[0x20B]);
#endif
				// Technically the IRQ is only triggered after the next instruction
				CPU.DoIRQ();		//irq6502();
				}

			return;
			}
		}

	// 2016-06-18 - Reset kbd irq if no key pressed
	// NO - "irqst is latched, only reset by write to IRQEN"
	//irqst |= 0x40;

	// If no keys are down at all, we can write anything again
	which->lastRead = 0xFF;

	// This should in theory work but in practise breaks some games?
	//memory5200[KBCODE] = which->lastRead = 0xFF;

#ifdef _DEBUG
//	DebugPrint("do_keys(): KBCODE = %02X\n", memory5200[KBCODE]);
#endif
}

// Read from GTIA registers
uint8 C5200::GTIAread(uint16 addr)
{
		uint8 GTIAreg = addr & 0x1f;
		switch (GTIAreg) {
		case 0x00:	//	M0PF	(missile to playfield collision)
				return M2PF[0];
				break;
		case 0x01:	//	M1PF
				return M2PF[1];
				break;
		case 0x02:	//	M2PF
				return M2PF[2];
				break;
		case 0x03:	//	M3PF
				return M2PF[3];
				break;
		case 0x04:	//	P0PF	(player to playfield collision)
				return P2PF[0];
				break;
		case 0x05:	//	P1PF
				return P2PF[1];
				break;
		case 0x06:	//	P2PF
				return P2PF[2];
				break;
		case 0x07:	//	P3PF
				return P2PF[3];
				break;
		case 0x08:	//  M0PL	(missile to player collisions)
				return M2PL[0];
				break;
		case 0x09:	//  M1PL
				return M2PL[1];
				break;
		case 0x0A:	//  M2PL
				return M2PL[2];
				break;
		case 0x0B:	//  M3PL
				return M2PL[3];
				break;
		case 0x0C:	//	P0PL	(player to player collisions)
				return P2PL[0];
				break;
		case 0x0D:	//	P1PL
				return P2PL[1];
				break;
		case 0x0E:	//	P2PL
				return P2PL[2];
				break;
		case 0x0F:	//	P3PL
				return P2PL[3];
				break;
		case 0x10:	//	TRIG0
				// JH 6/5/2002 - implement latches!!!
				// LATCHING: if GRACTL bit 2 set, then if joystick trigger pressed,
				// then TRIGn will be set to 0, and will stay that way.
				if (!(GRACTL & 0x040)) trig[0] = 1;  // if latch off, reset trigger
				if (controller1.trig) trig[0] = 0;
				return trig[0];
				break;
		case 0x11:	//	TRIG1
				// JH 6/5/2002 - implement latches!!!
				// LATCHING: if GRACTL bit 2 set, then if joystick trigger pressed,
				// then TRIGn will be set to 0, and will stay that way.
				if (!(GRACTL & 0x040)) trig[1] = 1;  // if latch off, reset trigger
				if (controller2.trig) trig[1] = 0;
				return trig[1];
				break;
		case 0x12:	//	TRIG2
				return trig[2];
				break;
		case 0x13:	//	TRIG3
				return trig[3];
				break;
		case 0x14:	//	PAL/NTSC
				// if PAL, then this will return 0x01
				// if NTSC, then return 0x0F
				// if a CART is PAL-compatible it will have 0x02
				// at 0xBFE7.
				//            fprintf(stderr, "PAL/NTSC flag read!\n");
				//timerTicks = 0;			// bounce out to debugger
				return options.videomode;		// default: NTSC
				break;
		case 0x15:	//	COLPM3
		case 0x16:	//	COLPF0
		case 0x17:	//	COLPF1
		case 0x18:	//	COLPF2
		case 0x19:	//	COLPF3
		case 0x1A:	//	COLBK
		case 0x1B:	//	PRIOR
		case 0x1C:	//	VDELAY
		case 0x1D:	//	GRACTL
		case 0x1E:	//	HITCLR
				return 0x0F;
				break;
		case 0x1F:	//	CONSOL (write-only in 5200?)
#ifdef DEBUG
			fprintf(stderr, "GTIA read of CONSOL\n");
#endif
			//return memory5200[CONSOL];       // JH 28/1/2002
				// JH 24/7/2014 - to handle code ported from A8 
				return 0xF;
				break;
		}

		// Oops!
		return 0;
}

/*
// Read from ANTIC regs
uint8 C5200::ANTICread(uint16 addr)
{
		uint8 ANTICreg = addr & 0xf;
		switch (ANTICreg) {
		case 0x00:	// DMACTL
		case 0x01:	// CHACTL
		case 0x02:	// DLISTL
		case 0x03:	// DLISTH
		case 0x04:	// HSCROL
		case 0x05:	// VSCROL
				return 0xFF;
				break;
		case 0x06:	// unused?
#ifdef DEBUG
			fprintf(stderr, "ANTIC read of unused (reg 6)\n");
#endif
			return 0xFF;
				break;
		case 0x07:	// PMBASE
				return 0xFF;
				break;
		case 0x08:	// unused?
#ifdef DEBUG
			fprintf(stderr, "ANTIC read of unused (reg 8)\n");
#endif
			return 0xFF;
				break;
		case 0x09:	// CHBASE
				return 0xFF;
				break;
		case 0x0A:	// WSYNC
				return 0xFF;
				break;
		case 0x0B:	// VCOUNT
				// Note: some games require accurate VCOUNT values
				//       I don't have it 100% accurate yet
				// return upper 8 bits of 9-bit counter
				// (ie: scanline / 2)
				//return (vcount / 2 + 4);              // gets Star Raiders working!
				if (vcount < 0) return 0;
				else
						return (vcount >> 1) & 0xFF;            // JH 15/3/2002
				break;
		case 0x0C:	// PENH
				return 0x80;				// hack
				break;
		case 0x0D:	// PENV
				return 0x80;				// hack
				break;
		case 0x0E:	//	NMIEN (write-only)
				return 0xFF;
				break;
		case 0x0F:	//	NMIST
				return antic.nmist;
				break;
		}

		// Oops!
		return 0;
}
*/

// read from expansion adapter
// (Not implemented)
// (Is expansion port used by anything?]
uint8 C5200::IOEXPread(uint16 addr)
{
		return 0;
}

// Read POKEY regs
uint8 C5200::POKEYread(uint16 addr)
{
		//int potval;
		uint8 skstatreg;
		uint8 POKEYreg = addr & 0xf;

		if (POKEYreg <= 0x7) {
				CController* which = NULL;
				switch (POKEYreg) {
				case 0:
				case 1:
						which = &controller1; break;
				case 2:
				case 3:
						which = &controller2; break;
						// 3 and 4 in the future
				default:
						return 0x80;
				}

				// Top to bottom
				if (POKEYreg & 1) {
						if (which->fake_mouse_support) {
								if (which->up) {
										if (which->vpos > 5) which->vpos -= 4;
								} else if (which->down) {
										if (which->vpos < 223) which->vpos += 4;
								} else {
										if (which->vpos < POT_CENTRE)
												which->vpos += 4;
										else if (which->vpos > POT_CENTRE)
												which->vpos -= 4;

										if (POT_CENTRE - which->vpos < 4)
												which->vpos = POT_CENTRE;
								}
								return which->vpos;
						} else {
								// JH 6/2002 - provision for analog/digital mode
								if (which->mode == CONT_MODE_ANALOG) {   // analog
										return which->analog_v;
								} else {
										if (which->up) return pot_max_left;
										else if (which->down) return pot_max_right;
										else
												return POT_CENTRE;
								}
						}
				} else {
						if (which->fake_mouse_support) {
								if (which->left) {
										if (which->hpos > 5) which->hpos -= 4;
								} else if (which->right) {
										if (which->hpos < 223) which->hpos += 4;
								} else {
										if (which->hpos < POT_CENTRE)
												which->hpos += 4;
										else if (which->hpos > POT_CENTRE)
												which->hpos -= 4;

										if (POT_CENTRE - which->hpos < 4)
												which->hpos = POT_CENTRE;
								}
								return which->hpos;
						} else {
								// JH 6/2002 - provision for analog/digital mode
								if (which->mode == CONT_MODE_ANALOG) {   // analog
										return which->analog_h;
								} else {
										if (which->left) return pot_max_left;
										else if (which->right) return pot_max_right;
										else
												return POT_CENTRE;
								}
						}
				}
		}

		// Other pokey registers
		switch (POKEYreg) {
		case 0x08:	//	ALLPOT
				//printf("POKEY read of ALLPOT\n");
				// this is the valid (finished) state of the POT readings
				return 0xFF;		// HACK - "always valid"
				break;
		case 0x09:	//	KBCODE
#ifdef _DEBUG
				DebugPrint("KBCODE read! (value = %X) PC = %4X\n", memory5200[KBCODE], CPU.GetPC());
#endif
				// key codes are fed to KBCODE by main loop,
				// and the KB int bit is set.
				return memory5200[KBCODE];
				break;
		case 0x0A :	//	RANDOM
				//fprintf(stderr, "RANDOM read!\n");
				// Note: return 8-bit random number
				return (rand() % 256);
				break;
		case 0x0B :	// UNUSED
		case 0x0C :	// UNUSED
		case 0x0D :	// UNUSED
				return 0xFF;
				break;
		case 0x0E :	//	IRQST
#ifdef _DEBUG
				DebugPrint("IRQST read! (value = %X) \n", m_irqst);
#endif
				return m_irqst;
				break;
		case 0x0F :	//	SKSTAT
				// Bit 3 of SKSTAT (read) is "shift key (top side button)"
				//      pressed (for selected controller) (0 = True).
				// Bit 2 of SKSTAT (read) is "last key still pressed" (0 = True).
				//  (or is it "any key pressed"?)
				// If kbd scanning disabled, then SKSTAT bit 2 reads 1.
				// (JH - implement!!!)
				/* was:
				skstatreg = 0;
				if(!controller1.side_button) skstatreg |= 0x08;
				if(!controller1.last_key_still_pressed) skstatreg |= 0x04;
				return skstatreg;
				*/
				skstatreg = 0x0C;
				switch(memory5200[CONSOL] & 0x03) {
				case 0 : // controller 1
						if(controller1.side_button) skstatreg &= 0x07;
						if(controller1.last_key_still_pressed) skstatreg &= 0x0B;
						break;
				case 1 : // controller 2
						if(controller2.side_button) skstatreg &= 0x07;
						if(controller2.last_key_still_pressed) skstatreg &= 0x0B;
						break;
				}
				return skstatreg;
				break;
		} // end switch

		// Oops!
		return 0;
}

// Write to GTIA regs
void C5200::GTIAwrite(uint16 addr, uint8 byte)
{
		uint8 GTIAreg = addr & 0x1f;

		switch (GTIAreg) {
		case 0x00:	//	HPOSP0
				memory5200[HPOSP0] = byte;
				break;
		case 0x01:	//	HPOSP1
				memory5200[HPOSP1] = byte;
				break;
		case 0x02:	//	HPOSP2
				memory5200[HPOSP2] = byte;
				break;
		case 0x03:	//	HPOSP3
				memory5200[HPOSP3] = byte;
				break;
		case 0x04:	//	HPOSM0
				memory5200[HPOSM0] = byte;
				break;
		case 0x05:	//	HPOSM1
				memory5200[HPOSM1] = byte;
				break;
		case 0x06:	//	HPOSM2
				memory5200[HPOSM2] = byte;
				break;
		case 0x07:	//	HPOSM3
				memory5200[HPOSM3] = byte;
				break;
		case 0x08:	//  SIZEP0
				memory5200[SIZEP0] = byte;
				break;
		case 0x09:	//  SIZEP1
				memory5200[SIZEP1] = byte;
				break;
		case 0x0A:	//  SIZEP2
				memory5200[SIZEP2] = byte;
				break;
		case 0x0B:	//  SIZEP3
				memory5200[SIZEP3] = byte;
				break;
		case 0x0C:	//	SIZEM
				memory5200[SIZEM] = byte;
				break;
		case 0x0D:	//	GRAFP0
				memory5200[GRAFP0] = byte;
				break;
		case 0x0E:	//	GRAFP1
				memory5200[GRAFP1] = byte;
				break;
		case 0x0F:	//	GRAFP2
				memory5200[GRAFP2] = byte;
				break;
		case 0x10:	//	GRAFP3
				memory5200[GRAFP3] = byte;
				break;
		case 0x11:	//	GRAFM
				memory5200[GRAFM] = byte;
				break;
		case 0x12:	//	COLPM0
				memory5200[COLPM0] = byte;
				break;
		case 0x13:	//	COLPM1
				memory5200[COLPM1] = byte;
				break;
		case 0x14:	//	COLPM2
				memory5200[COLPM2] = byte;
				break;
		case 0x15:	//	COLPM3
				memory5200[COLPM3] = byte;
				break;
		case 0x16:	//	COLPF0
				memory5200[COLPF0] = byte;
				break;
		case 0x17:	//	COLPF1
				memory5200[COLPF1] = byte;
				break;
		case 0x18:	//	COLPF2
				memory5200[COLPF2] = byte;
				break;
		case 0x19:	//	COLPF3
				memory5200[COLPF3] = byte;
				break;
		case 0x1A:	//	COLBK
				memory5200[COLBK] = byte;
				break;
		case 0x1B:	//	PRIOR
				memory5200[PRIOR] = byte;
				break;
		case 0x1C:	//	VDELAY
				// DEBUG - to see which games actually use this
				//printf("***** Write to VDELAY: %X\n", byte);
				memory5200[VDELAY] = byte;
				break;
		case 0x1D:	//	GRACTL
				memory5200[GRACTL] = byte;
#ifdef DEBUG
			fprintf(stderr, "Write to GRACTL: %X\n", byte);
#endif
			break;
		case 0x1E:	//	HITCLR (resets all collision regs)
				clear_collision_regs();
				break;
		case 0x1F:	//	CONSOL
				// NB: set PC speaker with consol bit 3
				// (Do any games use this?]
				if ((byte & 0x08) != (memory5200[CONSOL] & 0x08)) {
						//fprintf(stderr, "CONSOL bit 3 switched (spkr)\n");
				}
				memory5200[CONSOL] = byte;
#ifdef DEBUG
			fprintf(stderr, "Write to CONSOL: %X\n", byte);
#endif
			break;
		}
}

/*
// Write to ANTIC regs
void C5200::ANTICwrite(uint16 addr, uint8 byte)
{
		switch (addr) {
		case 0xD400:	//	DMACTL
				memory5200[DMACTL] = byte;
				break;
		case 0xD401:	//	CHACTL
				memory5200[CHACTL] = byte;
				break;
		case 0xD402:	//	DLISTL
				memory5200[DLISTL] = byte;
				break;
		case 0xD403:	//	DLISTH
				memory5200[DLISTH] = byte;
				break;
		case 0xD404:	//	HSCROL
				memory5200[HSCROL] = byte;
				break;
		case 0xD405:	//	VSCROL
				memory5200[VSCROL] = byte;
				break;
		case 0xD406:	//  UNKNOWN
				break;
		case 0xD407:	//	PMBASE
				memory5200[PMBASE] = byte;
				break;
		case 0xD408:	//  UNKNOWN
				break;
		case 0xD409:	//  CHBASE
				memory5200[CHBASE] = byte;
				break;
		case 0xD40A:	//  WSYNC
				// writing to WSYNC halts CPU until 7 machine cycles
				// (14 color clocks) before next scan line, to prevent
				// changes to current scan line from DLI routine
				// In an emulator, we just 'count' off the cycles
				// remaining in this scanline.
				// NB: modified to keep updating ANTIC 10x4 cycles
				// (otherwise line draw may be missed!)
				// tris = ticks remaining in scanline
				// OPTIMISE!
				// JH 28/1/2002: timing info:
				//      Wide PF: HBLANK = 18 machine cycles
				//      Normal PF: HBLANK = 34 machine cycles
				//      Narrow PF: HBLANK = 50 machine cycles
				//while(ticksToHSYNC > 10) {
				//updateANTIC(4);
				//}
				if (ticksToHSYNC < 10) updateANTIC();
				ticksToHSYNC = 7;
				if (ticksTillDraw < 100) ticksTillDraw = -1;
				break;
		case 0xD40B:	//  VCOUNT
				break;
		case 0xD40C:	//  PENH
				break;
		case 0xD40D:	//  PENV
				break;
		case 0xD40E:	//	NMIEN
				antic.nmien = byte;
				memory5200[NMIEN] = byte;
				break;
		case 0xD40F:	//	NMIRES
				antic.nmist = 0x0;
				memory5200[NMIRES] = 0x0;
				break;
		}
}
*/

// Write to expansion port
// (not implemented)
void C5200::IOEXPwrite(uint16 addr, uint8 byte)
{
		// ???
}

#ifdef DEBUG
#define LOG_POKEY_WRITE
#endif

// Write to POKEY regs
void C5200::POKEYwrite(uint16 addr, uint8 byte)
{
		switch (addr & 0x0F) {
		case 0x00:	//	AUDF1
		case 0x02:	//	AUDF2
		case 0x04:	//	AUDF3
		case 0x06:	//	AUDF4
		case 0x08:	//	AUDCTL
				// write value to sound POKEYSOUND emulation
				//Update_pokey_sound (uint16 addr, uint8 val, uint8 chip, uint8 gain)
				pokey.Update((uint16)(addr & 0x0F), byte, 0, 64);
#ifdef LOG_POKEY_WRITE
			sprintf(logmsg, "POKEY WRITE: vcount = %d\n", vcount);
			HostLog(logmsg);
#endif
			break;
		case 0x01:	//	AUDC1
		case 0x03:	//	AUDC2
		case 0x05:	//	AUDC3
		case 0x07:	//	AUDC4
				// sample event (for voice emulation)?
				if ((byte & 0xF0) == 0x10) {
						addSampleEvent(antic.m_vcount, ((addr & 0x0F) - 1) / 2, (uint8)(byte & 0x0F));
#ifdef VOICE_DEBUG
				sprintf(logmsg, "SE: %d\t%d\t%02X\n", vcount, ((addr & 0x0F)-1)/2, byte);
				HostLog(logmsg);
#endif
			} else {
						// write value to sound POKEYSOUND emulation
						//Update_pokey_sound (uint16 addr, uint8 val, uint8 chip, uint8 gain)
						pokey.Update((uint16)(addr & 0x0F), byte, 0, 64);
				}
				//fprintf(logfile, "Write to POKEY: %X, %X\n", addr, byte);
#ifdef LOG_POKEY_WRITE
			sprintf(logmsg, "POKEY WRITE: vcount = %d\n", vcount);
			HostLog(logmsg);
#endif
			break;
		case 0x09:	//	STIMER
				// writing any non-zero val will reset audio clocks to
				// values in AUDFn
				// (not implemented)
#ifdef DEBUG
			fprintf(stderr, "Write to STIMER: %X\n", byte);
#endif
			break;
		case 0x0A:	//
				//printf("POKEY write to reg A with byte %X\n", byte);
				break;
		case 0x0B:	//	POTGO
#ifdef _DEBUG
			//DebugPrint("POTGO written at vcount=%d\n", vcount);
#endif
			// This restarts the POT "counters"
				// do nothing
				break;
		case 0x0C:	//
				//printf("POKEY write to reg C with byte %X\n", byte);
				break;
		case 0x0D:	//
				//printf("POKEY write to reg D with byte %X\n", byte);
				break;
		case 0x0E:	//	IRQEN/IRQST
				// IRQEN (and NMIEN) enable interrupts when bits are logic 1
				// IRQST (unlike NMIST) bits are normally 1, pulled to 0 to indicate an interrupt.
				// IRQST bits are returned to 1 by writing 0 into the corresp. IRQEN bit.
				// This will disable the relevant interrupt.
				// Bit 3 of IRQST is not a latch, does not get set to 1. (Serial empty bit)
				m_irqen = byte;
				// check for IRQST resets (just bit 7 & 6 for now)
				if(!(byte & 0x80)) m_irqst |= 0x80;
				if(!(byte & 0x40)) m_irqst |= 0x40;
				// for completeness (JH 28/1/2002)
				if(!(byte & 0x20)) m_irqst |= 0x20;
				if(!(byte & 0x10)) m_irqst |= 0x10;
				// For more completeness (JH 2016-06-11)
				if(!(byte & 0x04)) m_irqst |= 0x04;
				if(!(byte & 0x02)) m_irqst |= 0x02;
				if(!(byte & 0x01)) m_irqst |= 0x01;

				//// check for keyboard or button int
				// 2016-06-18 - Not immediately - interrupt now triggered in updateANTIC() (every 32 scan lines)
				//if(irqen & 0xc0)
				//	do_keys();
#ifdef _DEBUG
		DebugPrint("Write to IRQEN: %X (vcount %d frame %d)\n", byte, antic.m_vcount, m_framesdrawn);
#endif
			break;
		case 0x0F:	//	SKCTL
				// Bit 0 of SKCTL (debounce enable) need not be set in the 5200.
				// Not only is the debounce enable not needed, if you do enable it you won�t be
				// able to read any keycodes back from the POKEY.
				// SKCTL bit 2 (last key still pressed) does not behave as expected on the 5200.
				// I have not yet determined it�s actual behavior.
				// SKCTL bit 1 enables the keyboard scanning. Note that this also 
				// enables/disables the POT scanning. 
				// TODO - Only update KBCODE if scanning enabled
				memory5200[SKCTL] = byte;
#ifdef _DEBUG
		DebugPrint("Write to SKCTL: %X\n", byte);
#endif
			break;
		} // end switch
}

// memory CPU read (load) handler
uint8 get6502memory(uint16 addr)
{
		// Note: memory read handling could be sped up by
		//       using an array of function pointers.
		//			(instead of switch statement).
		switch (mapper[addr]) {
		case MAPPER_INVALID:	// invalid address
				//printf("Invalid read, address %4X\n", addr);
				break;
		case MAPPER_RAM:	// RAM read
		case MAPPER_ROM:	// ROM read
				return(memory5200[addr]);
				break;
		case MAPPER_GTIA:	// GTIA read
				return jum52.GTIAread(addr);
				break;
		case MAPPER_ANTIC:	// ANTIC read
				return jum52.antic.Read(addr);
				break;
		case MAPPER_IOEXP:	// I/O exp read
				return jum52.IOEXPread(addr);
				break;
		case MAPPER_POKEY:	// POKEY read
				return jum52.POKEYread(addr);
				break;
		default:	// oops
				//printf("MAPPER ERROR!\n");
				//exit(1);
				break;
		}	// end switch

		return 0;
}

// reset sample events
void clearSampleEvents(void)
{
		numSampleEvents[0] = numSampleEvents[1] = numSampleEvents[2] = numSampleEvents[3] = 0;
}

// add a new sample event
void addSampleEvent(int vcount, int channel, unsigned char value)
{
		if (numSampleEvents[channel] < MAX_SAMPLE_EVENTS) {
				sampleEvent[channel][numSampleEvents[channel]].vcount = vcount;
				sampleEvent[channel][numSampleEvents[channel]].value = value;
				numSampleEvents[channel]++;
		}
}

// render sample output and mix with pokey output buffer
void renderMixSampleEvents(unsigned char* pokeyBuf, uint16 size)
{
		int ch, i, currBufPos, nextEventPos;
		int vc;
		float stepSize;

		if (options.videomode == PAL)
				stepSize = (float)snd_buf_size / 312;
		else
				stepSize = (float)snd_buf_size / 262;

		// empty out voiceBuffer
		currBufPos = 0;
		while (currBufPos < snd_buf_size)
				voiceBuffer[currBufPos++] = 0;

		// render sample events on all 4 channels
		for (ch = 0; ch < 4; ch++) {
				if (0 == numSampleEvents[ch])
						continue;

				currBufPos = 0;
				for (i = 0; i < numSampleEvents[ch]; i++) {
						vc = sampleEvent[ch][i].vcount;
						// correct timing of sample event
						if (vc > 248) vc -= 248;
						else
								vc += 14;
						nextEventPos = (int)(stepSize * (float)(vc));
						while (currBufPos < nextEventPos && currBufPos < snd_buf_size) {
								voiceBuffer[currBufPos++] += currVoiceVal[ch] - 8;				// JH 20/3/2012 - changed to -8
						}
						if (currBufPos > snd_buf_size)
								break;
						// update current sample level
						currVoiceVal[ch] = sampleEvent[ch][i].value;
				} // next i

				// fill up remainder of buffer with current level
				while (currBufPos < snd_buf_size)
						voiceBuffer[currBufPos++] += currVoiceVal[ch] - 8;				// JH 20/3/2012 - changed to -8

		} // next channel


		// mix sample output with POKEY output buffer
		for (i = 0; i < snd_buf_size; i++) {
				//		snd[i] += voiceBuffer[i];
				snd[i] += voiceBuffer[i] * 3;
		}

#ifdef VOICE_DEBUG
	sprintf(logmsg, "***** Num samples events: %d %d %d %d\n", numSampleEvents[0], numSampleEvents[1], numSampleEvents[2], numSampleEvents[3]);
	HostLog(logmsg);
#endif

		clearSampleEvents();
}

// memory CPU write (store) handler
void put6502memory(uint16 addr, uint8 byte)
{
		// Note: memory write handling could be sped up
		//			by using an array of function pointers.
		switch (mapper[addr]) {
		case MAPPER_INVALID:	// invalid address
#ifdef DEBUG
			fprintf(stderr, "Invalid write, address %4X\n", addr);
#endif
			break;
		case MAPPER_RAM:	// RAM write
				memory5200[addr] = byte;
				break;
		case MAPPER_ROM:	// ROM write (!!!)
#ifdef DEBUG
			fprintf(stderr, "ROM write, address %4X\n", addr);
#endif
			break;
		case MAPPER_GTIA:	// GTIA write
				jum52.GTIAwrite(addr, byte);
				break;
		case MAPPER_ANTIC:	// ANTIC write
				jum52.antic.Write(addr, byte);
				break;
		case MAPPER_IOEXP:	// I/O exp write
				jum52.IOEXPwrite(addr, byte);
				break;
		case MAPPER_POKEY:	// POKEY write
				jum52.POKEYwrite(addr, byte);
				break;
		default:	// oops
				//printf("MAPPER ERROR!\n");
				//exit(1);
				break;
		}	// end switch
}

// load CARTRIDGE into memory image
// Use crc32 to identify carts! - JH
//int loadCART(char *cartname)
int C5200::loadCART(char* cartname)
{
	int i, mapnum, flen;
	char sig[40];
	unsigned long crc32;
	FILE* pfile;

	pfile = fopen(cartname, "rb");
	if (pfile == NULL)
		{
		sprintf(errormsg, "Unable to open cartridge ROM file %s - check if it's there!", cartname);
		HostLog(errormsg);
		return -1;
	}

	// get file length
	fseek(pfile, 0, SEEK_END);
	flen = ftell(pfile);
	rewind(pfile);

	// set POT left and right values to default
	pot_max_left = POT_LEFT;
	pot_max_right = POT_RIGHT;

	// load cart into memory image
	// Note: 5200 cartridge mapping has only a few
	//       variations, so this mess of code below
	//       works, and it avoids having a cartridge
	//       config file.
	switch (flen)
		{
		case 32768:	// 32k cart
			for (i = 0; i < 32768; i++)
				memory5200[0x4000 + i] = fgetc(pfile);
			// get crc32 from 32k data
			crc32 = calc_crc32(memory5200 + 0x4000, 32768);
			sprintf(errormsg, "Trying to load '%s', crc32=0x%08X\n", cartname, crc32);
			HostLog(errormsg);
			break;
		case 16384:	// 16k cart
			// here we hack and load it twice (mapped like that?)
			for (i = 0; i < 16384; i++)
				{
				uint8 c = fgetc(pfile);
				memory5200[0x4000 + i] = c;
				memory5200[0x8000 + i] = c;
				}

			// get crc32 from 16k data
			crc32 = calc_crc32(memory5200 + 0x4000, 16384);
			sprintf(errormsg, "Trying to load '%s', crc32=0x%08X\n", cartname, crc32);
			HostLog(errormsg);

			// get cart "signature"
			strncpy(sig, (const char*)&memory5200[0xBFE8], 20);
			sig[20] = 0;
			//printf("Cart signature is %s\n", sig);

			// check for Moon Patrol
			if (strcmp("@@@@@moon@patrol@@@@", sig) == 0)
				{
				//printf("Mapping for Moon Patrol  (16+16)\n");
				// already loaded correctly
				break;
				}

			// check for SW-Arcade
			if (strncmp("asfilmLt", sig, 8) == 0)
				{
				//printf("Mapping for SW-Arcade  (16+16)\n");
				// already loaded correctly
				break;
				}

			// check for Super Pacman using start vector
			if ((memory5200[0xBFFF] == 0x92) && (memory5200[0xBFFE] == 0x55))
				{
				//printf("Mapping for Super Pacman  (16+16)\n");
				// already loaded correctly
				break;
				}

			// check for other carts with reset vec 8000h
			// (eg: Space Shuttle)
			if (memory5200[0xBFFF] == 0x80)
				{
				//printf("Mapping for reset vec = 8000h  (16+16)\n");
				// already loaded corectly
				break;
				}

			// Tempest
			if (memory5200[0xBFFF] == 0x81)
				{
				//printf("Mapping for reset vec = 81xxh eg: Tempest (16+16)\n");
				// already loaded corectly
				break;
				}

			// PAM Diagnostics v2.0
			// NB: this seems to prevent the emu from crashing when running
			// pamdiag2.bin
			if ((memory5200[0xBFFF] == 0x9F) && (memory5200[0xBFFE] == 0xD0))
				{
				//printf("Mapping for reset vector = $9FD0 (PAM DIAG 2.0)\n");
				// move cart up by 0x1000
				break;
				}

			// Notes: check for megamania cart
			// 8K mirrored at 0x8000 and 0xA000, nothing from 0x4000-0x7FFF

			// see if we have a 16k mapping for this cart in jum52.cfg
			sprintf(sig, "%08X", crc32);
			mapnum = 0; // invalid
			for (i = 0; i < num16kMappings; i++)
				{
				if (0 == strncmp(sig, p16kMaps[i].crc, 8))
					{
					mapnum = p16kMaps[i].mapping;
					sprintf(errormsg, "Mapping %d found for crc=0x%s !\n", mapnum, sig);
					HostLog(errormsg);
					i = num16kMappings; // exit search
					}
				}
			// if the mapping was 16+16, then break, since we have loaded it 16+16 already
			if (1 == mapnum)
					break;

			// default to 16k+8k mapping
			fseek(pfile, 0, SEEK_SET);
			for(i=0; i<16384; i++) memory5200[0x6000 + i] = fgetc(pfile);
			for(i=0; i<8192; i++) memory5200[0xA000 + i] = memory5200[0x8000 + i];
			break;
		case 8192 :	// 8k cart
			// Load mirrored 4 times
			for(i = 0; i < 8192; i++)
				{
				uint8 c = fgetc(pfile);
				memory5200[0x4000 + i] = c;
				memory5200[0x6000 + i] = c;
				memory5200[0x8000 + i] = c;
				memory5200[0xA000 + i] = c;
				}
			// get crc32 from 8k data
			crc32 = calc_crc32(memory5200 + 0x4000, 8192);
			sprintf(errormsg, "8k cart load '%s', crc32=0x%08X\n", cartname, crc32);
			HostLog(errormsg);
			break;
		case 4096 :	// 4k cart (yellow sub demo)
			// Load mirrored 8 times
			for(i = 0; i < 4096; i++)
				{
				uint8 c = fgetc(pfile);
				memory5200[0x4000 + i] = c;
				memory5200[0x5000 + i] = c;
				memory5200[0x6000 + i] = c;
				memory5200[0x7000 + i] = c;
				memory5200[0x8000 + i] = c;
				memory5200[0x9000 + i] = c;
				memory5200[0xA000 + i] = c;
				memory5200[0xB000 + i] = c;
				}
			// get crc32 from 4k data
			crc32 = calc_crc32(memory5200 + 0x4000, 4096);
			sprintf(errormsg, "4k cart load '%s', crc32=0x%08X\n", cartname, crc32);
			HostLog(errormsg);
			break;
		default:		// oops!
			// these rom dumps are strange, because some carts are 8K, yet
			// all the dumps are either 16K or 32K!
			sprintf(errormsg, "Cartridge ROM size not 16K or 32K. Unable to load.");
			return -1;
			break;
		}

	// check for Pengo
	if (strncmp("pengo", (const char*)(memory5200 + 0xBFEF), 8) == 0)
		{
		HostLog("Pengo detected! Switching controller to Pengo mode.\n");
		pot_max_left = 70;
		pot_max_right = 170;
		}

	// is cartridge PAL-compatible?
	// (doesn't seem to work!)
	//if(memory5200[0xBFE7] == 0x02) printf("Cart is PAL-compatible!\n");
	//else printf("Cart is *not* PAL-compatible.\n");

	fclose(pfile);

	return 0;
}

// load in a new palette from .act palette file
int loadPalette(char* filename)
{
		int i;
		FILE* pfile;

		sprintf(errormsg, "Loading palette file '%s'...\n", filename);
		HostLog(errormsg);

		pfile = fopen(filename, "rb");
		if (pfile == NULL) {
				HostLog("Unable to open palette file! - Check if it's there!\n");
				return -1;
		}

		for (i = 0; i < 256; i++) {
				uint8 r = fgetc(pfile);
				uint8 g = fgetc(pfile);
				uint8 b = fgetc(pfile);
				uint32 rgb32 = (r << 16) | (g << 8) | b;
				colourtable[i] = rgb32;
				//HostSetPaletteEntry(i, r, g, b);
		}

		fclose(pfile);

		return 0;
}

// Helper function - make string uppercase
static void makeUpperCase(char* s)
{
		char* p = s;
		while (*p) {
				*p = toupper(*p);
				p++;
		}
}

// Helper function - make string lowercase
static void makeLowerCase(char* s)
{
		char* p = s;
		while (*p) {
				*p = tolower(*p);
				p++;
		}
}

// add a 16k rom mapping
void addRomMapping(char* crc, int mapnum, char* description)
{
		if (num16kMappings < NUM_16K_ROM_MAPS) {
				strncpy(p16kMaps[num16kMappings].crc, crc, 8);
				//strupr(p16kMaps[num16kMappings].crc);
				makeUpperCase(p16kMaps[num16kMappings].crc);
				p16kMaps[num16kMappings].mapping = mapnum;
				strncpy(p16kMaps[num16kMappings].description, description, 64);
				num16kMappings++;
		}
}

// add a key remapping
void addKeyMapping(unsigned short input, unsigned short output)
{
		if (input < KEY_MAP_SIZE)
				keyMap[input] = output;
}

// load in config defaults from config file
int LoadConfigFile(void)
{
		int i;
		FILE* pfile;
		char line[256];
		char name[32];
		char value[256];
		char* p;

		HostLog("Reading config file 'jum52.cfg'...\n");

		pfile = fopen("jum52.cfg", "r");
		if (pfile == NULL) {
				sprintf(errormsg, "Unable to open config file 'Jum52.cfg' - check if it's there!");
				HostLog(errormsg);
				return -1;
		}

		// Read line-by-line
		while (NULL != fgets(line, 256, pfile)) {
				// skip comment lines
				if (line[0] == '#')
						continue;
				// skip empty lines
				if (strlen(line) < 4)
						continue;
				// get option name
				i = 0;
				while (line[i] != ' ' && line[i] != '=') {
						name[i] = line[i];
						i++;
				}
				name[i] = 0;
				makeLowerCase(name);					// convert to lowercase
				// get option value
				p = line + i + 1;
				i = 0;
				// skip any spaces
				while (p[i] == ' ' || p[i] == '=') i++;
				// read value
				p += i;
				i = 0;
				while (p[i] != 0 && p[i] != ' ' && p[i] != 0x0A && p[i] != 0x0D) {
						value[i] = p[i];
						i++;
				}
				value[i] = 0;
				makeLowerCase(value);					// convert to lowercase

				// try match it and apply it if we get a match
				if (0 == strcmp(name, "videomode")) {
						// default is ntsc
						if (0 == strcmp(value, "pal"))
								options.videomode = PAL;
				} else if (0 == strcmp(name, "debug")) {
						// default is 0
						if (0 == strcmp(value, "yes"))
								options.debugmode = 1;
				} else if (0 == strcmp(name, "audio")) {
						// default is 1
						if (0 == strcmp(value, "no"))
								options.audio = 0;
				} else if (0 == strcmp(name, "voice")) {
						// default is 1
						if (0 == strcmp(value, "no"))
								options.voice = 0;
				} else if (0 == strcmp(name, "volume")) {
						options.volume = atoi(value);
						if (options.volume > 100)
								options.volume = 100;
				} else if (0 == strcmp(name, "fullscreen")) {
						// default is 0
						if (0 == strcmp(value, "yes"))
								options.fullscreen = 1;
				} else if (0 == strcmp(name, "controller")) {
						// defaults to keyboard (0)
						if (0 == strcmp(value, "joystick"))
								options.controller = 1;
						else if (0 == strcmp(value, "mouse"))
								options.controller = 2;
						else if (0 == strcmp(value, "mousepaddle"))
								options.controller = 3;
				} else if (0 == strcmp(name, "controlmode")) {
						// defaults to normal (0)
						if (0 == strcmp(value, "robotron"))
								options.controlmode = 1;
						else if (0 == strcmp(value, "pengo"))
								options.controlmode = 2;
				} else if (0 == strcmp(name, "palette")) {
						loadPalette(value);
				} else if (0 == strcmp(name, "scale")) {
						// JH 2014-07-27 - Only if not in debugger mode
						if (0 == options.debugmode) {
								options.scale = atoi(value);
								if (options.scale > 4)
										options.scale = 4;
								else if (options.scale < 1)
										options.scale = 1;
						}
				} else if (0 == strcmp(name, "scanlines")) {
						// defaults to 0
						options.scanlines = atoi(value);
				} else if (0 == strcmp(name, "slow")) {
						// defaults to 0
						if (0 == strcmp(value, "yes"))
								options.slow = 1;
				} else if (0 == strcmp(name, "map")) {
						// 16k rom mapping
						addRomMapping(value, value[9] - 48, value + 11);
				} else if (0 == strcmp(name, "keymap")) {
						// keyboard remapping
						int input = 0;
						int output = 0;
						sscanf(value, "%d,%d", &input, &output);
						addKeyMapping((unsigned short)input, (unsigned short)output);
				}
		}

		fclose(pfile);

		return 0;
}

// init Jum52 engine
//int Jum52_Initialise(void)
int C5200::Initialise()
{
		HostLog("Initialising Jum52...\n");

		// initialise 16k rom maps
		p16kMaps = (Map16k_t*)malloc(sizeof(Map16k_t) * NUM_16K_ROM_MAPS);
		if (p16kMaps) memset(p16kMaps, 0, sizeof(Map16k_t) * NUM_16K_ROM_MAPS);

		num16kMappings = 0;

		// initialise key mapping
		int i;
		for (i = 0; i < KEY_MAP_SIZE; i++)
				keyMap[i] = (unsigned short)i;

		// read Jum52 config
		LoadConfigFile();

		// Graphics engine
		if (init_gfx() != 0) {
#ifdef PS2DEBUG
		HostLog("init_gfx() failed!\n");
#endif
		return -1;
		}

		// Set up mappers - everything is invalid first off...
		mapper = (uint8*)malloc(0x10000);
		if (mapper == NULL) {
#ifdef PS2DEBUG
		HostLog("mapper malloc() failed!\n");
#endif
		return -1;
		}

		memset(mapper, MAPPER_INVALID, 0x10000);
		for (i = 0; i < 0x4000; i++) mapper[i] = MAPPER_RAM;         // RAM
		for (i = 0x4000; i < 0xC000; i++) mapper[i] = MAPPER_ROM;    // CART
		for (i = 0xF800; i < 0x10000; i++) mapper[i] = MAPPER_ROM;   // BIOS
		for (i = 0xC000; i < 0xC100; i++) mapper[i] = MAPPER_GTIA;   // GTIA
		for (i = 0xD400; i < 0xD500; i++) mapper[i] = MAPPER_ANTIC;   // ANTIC
		for (i = 0xE800; i < 0xE900; i++) mapper[i] = MAPPER_POKEY;   // POKEY
		for (i = 0xEB00; i < 0xEC00; i++) mapper[i] = MAPPER_POKEY;   // POKEY (mirror)

		// Set up memory area
		memory5200 = (uint8*)malloc(0x10000);
		if (memory5200 == NULL) {
#ifdef PS2DEBUG
		HostLog("memory5200 malloc() failed!\n");
#endif
		return -1;
		}

		memset(memory5200, 0, 0x10000);

		// Set up sound output
		pokey.Initialise(FREQ_17_APPROX, SND_RATE, 1);
		if (options.videomode == PAL)
				snd_buf_size = SND_RATE / 50;
		else
				snd_buf_size = SND_RATE / 60;
		snd = (uint8*)malloc(snd_buf_size);
		if (snd == NULL) {
#ifdef PS2DEBUG
		HostLog("snd malloc() failed!\n");
#endif
		return -1;
		}

		voiceBuffer = (int8*)malloc(snd_buf_size);
		if (voiceBuffer == NULL) {
#ifdef PS2DEBUG
		HostLog("voiceBuffer malloc() failed!\n");
#endif
		return -1;
		}

		memset(snd, 0x80, snd_buf_size);
		memset(voiceBuffer, 0x00, snd_buf_size);

		// init controllers
		//memset(&controller1, 0, sizeof(CONTROLLER));
		//memset(&controller2, 0, sizeof(CONTROLLER));
		pot_max_left = POT_LEFT;
		pot_max_right = POT_RIGHT;

		return 0;
}

//int Jum52_LoadROM(char *path)
int C5200::LoadROM(char* path)
{
		int i;

		// Clear memory out altogether
		memset(memory5200, 0, 0x10000);

		// Load 5200 bios to address 0xF800
		for (i = 0; i < 2048; i++) {
				memory5200[0xF800 + i] = BIOSData[i];
		}

		// Try to load the cartridge
#ifdef _EE
	if (PS2_loadCART(path) != 0)
#else
	if (loadCART(path) != 0)
#endif
		return -1;

		CPU.Reset();									//reset6502();
		antic.Initialise();
		initGTIA();
		clear_collision_regs();

		reset_gfx();

		//for (i = 0; i < 10; i++)
		//		keypad[i] = 0;

		memset(snd, 0x80, snd_buf_size);

		memory5200[KBCODE] = 0x1F;			// req. for JUMPONG :)

		// Success
		return 0;
}

//int Jum52_Emulate(void)
int C5200::Emulate()
{
		// Prevent recursion
		if (isEmulating) {
				sprintf(errormsg, "A ROM is already being executed. You cannot start another one.");
				return -1;
		}

		clearSampleEvents();

		isEmulating = 1;
		//exec6502fast(2000000000);
		//CPU.RunFast(2000000000);
		RunFast(2000000000);

		isEmulating = 0;

		return 0;
}

//int Jum52_Reset()
int C5200::Reset()
{
		HostLog("Resetting 5200...\n");

		CPU.Reset();				//reset6502();
		reset_gfx();

		memset(memory5200, 0, 0x4000);		// Clear RAM
		memset(snd, 0x80, snd_buf_size);

		antic.Initialise();
		initGTIA();
		clear_collision_regs();

		controller1.analog_h = POT_CENTRE;
		controller1.analog_v = POT_CENTRE;
		controller2.analog_h = POT_CENTRE;
		controller2.analog_v = POT_CENTRE;

		return 0;
}

/*
// initialise ANTIC parameters
void C5200::initANTIC(void)
{
	clear_collision_regs();
	//tickstoVBI = 29829;					// not used
	ticksToHSYNC = CYCLESPERLINE;				// should be 114
	ticksTillDraw = 9999999;
	vcount = -2;
	//scanline = 0;
	next_dli = 300;
	vscroll = 0;
	hscroll = 0;
	scrolltrig = 0;
	vscrollStartLine = 0;
	next_mode_line = 0;
	bytes_per_line = 0;
	framesdrawn = 0;

	scanaddr = 0;
	stolencycles = 0;
	dladdr = 0;
	finished = 0; //display list finished?
	pfLineCount = 0;

	// init IRQ's
	m_irqst = 0xFF;                // init to no IRQ state
	irqen = 0x0;                // no IRQ's enabled
	antic.nmien = 0;
	antic.nmist = 0;
}
*/
/*
// THIS FUNCTION CALLED EVERY HBL
// update ANTIC and check if time for VBI
void C5200::updateANTIC()
{
	antic.m_vcount++;

	// DEBUG
	//fprintf(stderr, "vcount=%d\n", vcount);

	// Reset VBI status bit on line 39
	if (39 == antic.m_vcount)
		{
		// reset VBI NMI interrupt
		antic.m_nmist &= 0xBF;
		memory5200[NMIST] = antic.m_nmist;
		}

	//// check for key IRQ just before VBI
	//// (otherwise it gets lost in VBI)
	//// (do_keys() commented out in code below)
	//if(vcount == KEYS_LINE || vcount == 120) {
	//		do_keys();				// get kbcode and set interrupt
	//}	

	// NB: 5200 will do a keyboard IRQ every 32 scanlines if a key is held down
	//     (and kbd IRQ is enabled)
	int vcount = antic.m_vcount;
	if (32 == vcount || 64 == vcount || 96 == vcount || 128 == vcount || 160 == vcount || 192 == vcount)
		{
		do_keys();				// get kbcode and trigger interrupt if neccessary
		}

	// do actual drawing area of screen
	if (vcount <= VBI_LINE) {
		// do DLI NMI if we are on the last line of a DL instr
		// with a DLI enabled
		// NB: this is sensitive!!!
		if (vcount == next_dli) {
			antic.m_nmist |= 0x80;
			memory5200[NMIST] |= 0x80;
			if (antic.m_nmien & 0x80) {
				CPU.DoNMI();		//nmi6502();
			}
		}

		// SCHEDULE DRAW SCANLINE for 40 CTIA cycles time
		// 20 CPU cycles
		antic.m_ticksTillDraw = 20;

		// do VBI if we are at line 248
		if (vcount == VBI_LINE) {
			// update sound
			//Pokey_process(snd, SND_BUF_SIZE);
			HostProcessSoundBuffer();

			// set off VBI by setting VBI status bit
			// and calling NMI (if enabled)
			antic.m_nmist = 0x40;			// HACK
			memory5200[NMIST] = 0x40;		// HACK

			// do VBI NMI if VBI enabled in NMIEN
			// NOTE: to get diagnostic cart working,
			// enable VBI NMI after x cycles
			if (antic.m_nmien & 0x40) {
				CPU.DoNMI();				//nmi6502();
			}

			// Do the video copy. Speed throttle goes here
			// Or in the HostProcessSoundBuffer routine.
			HostBlitVideo();
			// clear collision buffer
			memset(m_collbuff, 0, VID_WIDTH * VID_HEIGHT);


			finished = FALSE;
			framesdrawn++;
			pfLineCount = 0;

			// Controller/Event processing
			HostDoEvents();

			// RFB: What's this for?
			// JH: Joystick B button used for A5200 controller
			// "side buttons", which cause a BRK interrupt.
			//if(joy[0].button[1].b)
			//{
			//	m_irqst &= 0x7F;	// BRK key
			//	// check irqen and do interrupt if bit 7 set
			//	if(irqen & 0x80) irq6502();
			//}

		} // end if VBI
	} // end if vcount < 249
	else {
		// just check for end of frame
		// fiddling with this might get some games working???
		if (260 == vcount) {
			antic.m_vcount = -2;
			next_mode_line = 8;
			//scanline = 0;
			next_dli = 300;			// disable next dli until next frame
			// reload scan counter address
			dladdr = memory5200[DLISTL] + 256 * memory5200[DLISTH];
		}
	} // end else scanline < 249


}
*/