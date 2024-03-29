// Win32 / SDL Driver for Jum52 (v 1.3)
// Copyright James Higgs 2004 - 2018

// DESIGN - This file works with the C5200 class ("emu machine") 

#include "stdafx.h"
#include <stdlib.h>
#include <io.h>							// For reading folder/directory
#include <stdio.h>
#include "windows.h"
#include "SDL.h"
#include "SDL_syswm.h"

// Jum52 stuff
#include "../global.h"
#include "../5200.h"
#include "../savestate.h"
#include "../host.h"

#define JUM52_VERSION	"1.3"
#define VOICE_DEBUG
#define MAX_ROMS	200

enum {
	SCREENWIDTH = 320,
	SCREENHEIGHT = 240,
	SCREENBPP = 0,
	SCREENFLAGS = SDL_ANYFORMAT
	};

// global variables

extern uint8 SystemFont[];
extern unsigned char font5200[];
extern int g_nFiltered;

// Local variables
static FILE *logfile = NULL;
static SDL_SysWMinfo wmInfo;
static int outScreenWidth = 0;
static int outScreenHeight = 0;
static Uint32 screenFlags = 0;
static SDL_Surface *pSurface = NULL;
static SDL_Surface *pBuffer = NULL;
static SDL_AudioSpec reqSpec;
static SDL_AudioSpec givenSpec;
static SDL_AudioSpec *usedSpec;
static SDL_Joystick *pJoystick = NULL;
uint8 *gfxdest = NULL;
static int fps_on = 0;
static int fps = 0;
static int lastSecondFrame = 0;
static int lastSecondTick = 0;
static int exit_app = 0;
//static int gotcartlist = 0;
static int debugging = 0;
static char msg[256];

// 48kHz sample rate (see global.h)
// we need 800 samples for NTSC, 960 for PAL
//unsigned short audiobuffer16[2000];

//int controller2connected = 0;

#define FRAMERATE		(1)				 //control's frame rate.
#define DEBOUNCE_RATE   10
static Uint32 frameStart = 0;
static int jprintf_y = 8;

// ROM/Menu stuff
static struct ROMdata *romdata = NULL;
static int num_roms = 0;
static int selection = 0;
static int frame_position = 0;
static int currentromindex = 0;
static char string[256];
static char rompath[260];

struct ROMdata {
	char name[40]; // These values should be more than enough
	char filename[256];
};

// The emulator "machine" class
C5200 jum52;

// simple print funcs
void drawChar(char c, int x, int y, unsigned int colour);
void printXY(char *s, int x, int y, unsigned int colour);
int DoOptionsMenu(void);

// Debugging
int monitor(void);

/// Wait for a specified number of frames
static void Wait(int numframes)
{
	// TODO - implement for Windows
}


// ******************************************************************************
// SDL output helper funcs
// ******************************************************************************

// 8-bit only
static void SDL_SetPixel(SDL_Surface *pSurface, int x, int y, uint8 value)
{
	//determine position
	char *pPosition = (char*)pSurface->pixels;

	//offset by y
	pPosition += (pSurface->pitch * y);

	//offset by x
	pPosition += x;

	//copy pixel data
	*pPosition = value;
}

/* Unused
static SDL_Color SDL_GetPixel(SDL_Surface *pSurface, int x, int y)
{
	SDL_Color color;
	Uint32 col = 0;

	//determine position
	char *pPosition = (char*)pSurface->pixels;

	//offset by y
	pPosition += (pSurface->pitch * y);

	//offset by x
	pPosition += (pSurface->format->BytesPerPixel * x);

	//copy pixel data
	memcpy(&col, pPosition, pSurface->format->BytesPerPixel);

	//convert color
	SDL_GetRGB(col, pSurface->format, &color.r, &color.g, &color.b);
	return (color);
}
*/

// 8-bit only
static void hline(SDL_Surface *pSurface, int x1, int x2, int y, Uint8 col)
{
	Uint8 bpp;
	short len;
	int i;

	//determine position
	char *pPosition = (char*)pSurface->pixels;

	//offset by y
	pPosition += (pSurface->pitch * y);

	//offset by x
	bpp = pSurface->format->BytesPerPixel;

	if (x2 < x1)
		{
		pPosition += (bpp * x2);
		len = x1 - x2;
		}
	else
		{
		pPosition += (bpp * x1);
		len = x2 - x1;
		}
	//copy pixel data
	for (i = 0; i < len; i++)
		{
		*pPosition++ = col;
		}
}

// 8-bit only
static void vline(SDL_Surface *pSurface, int x, int y1, int y2, Uint8 col)
{
	Uint16 pitch;
	short len;
	int i;

	//determine position
	char *pPosition = (char*)pSurface->pixels;

	pitch = pSurface->pitch;

	//offset by y
	if (y2 < y1)
		{
		pPosition += (pitch * y2);
		len = y1 - y2;
		}
	else
		{
		pPosition += (pitch * y1);
		len = y2 - y1;
		}

	//offset by x
	pPosition += x;

	//copy pixel data
	for (i = 0; i < len; i++)
		{
		*pPosition = col;
		pPosition += pitch;
		}
}

// get next keypress (waits for a key) - returns 0 on quit
char getkey(void)
{
	char ret;
	SDL_Event event;

	//look for an event
	ret = -1;
	while (ret == -1)
		{
		while (SDL_PollEvent(&event))
			{
			switch (event.type)
				{
					case SDL_QUIT:
						ret = 0;
						break;

					case SDL_KEYDOWN:
						/* Handle key presses. */
						ret = event.key.keysym.sym;
						if (event.key.keysym.mod & KMOD_SHIFT)
							ret -= 32;
						break;
				}
			}
		}

	return ret;
}

static Uint8 *gAudioStream = NULL;
static volatile unsigned char fillSoundBufferDone = 1;			// to sync emu to audio stream

// sound mixer callback
static void fillsoundbuffer(void *userdata, Uint8 *stream, int len)
{
	// NOTE: oldValue is assigned but never used!
	static Uint8 oldValue = 128;

	const int vol64 = (options.volume * 63) / 100;

	for (int i = 0; i < len; i++)
		{
		// TODO: filter?
		char val = (char)snd[i] - 128;
		int ampedVal = (vol64 * val) / 64;
		//stream[i] = 128 + ampedVal;
		// simple filter code
		Uint8 newValue = 128 + ampedVal;
		stream[i] = newValue;
		//stream[i] = (oldValue + newValue) >> 1;
		//stream[i] = newValue - ((newValue - oldValue) / 2);
		oldValue = newValue;
		}

	gAudioStream = stream;
	fillSoundBufferDone = 1;
}


// ******************************************************************************
//
// Init the Win32 host video / audio / input
//
// ******************************************************************************
static int Init(void)
{
	char s[256];
	SDL_Color sdlColor;

	// set video mode
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER | SDL_INIT_JOYSTICK) < 0)
		{
		sprintf(s, "Couldn't initialize SDL: %s\n", SDL_GetError());
		HostLog(s);
		return -1;
		}

	//create a window
	outScreenWidth = SCREENWIDTH * options.scale;
	outScreenHeight = SCREENHEIGHT * options.scale;
	if (1 == options.debugmode)
		{
		// for debug mode, stick to 640x480
		outScreenWidth = SCREENWIDTH * 2;
		outScreenHeight = SCREENHEIGHT * 2;
		}
	screenFlags = SDL_ANYFORMAT;
	if (options.fullscreen)
		{
		screenFlags |= SDL_FULLSCREEN;
		options.scale = 2;
		// for fullscreen, stick to 640x480
		outScreenWidth = SCREENWIDTH * 2;
		outScreenHeight = SCREENHEIGHT * 2;
		}

	pSurface = SDL_SetVideoMode(outScreenWidth, outScreenHeight, SCREENBPP, screenFlags);
	if (pSurface == NULL)
		{
		sprintf(s, "Couldn't set SDL video mode: %s\n", SDL_GetError());
		HostLog(s);
		return -2;
		}

	// get window manager info (need hwnd later)
	SDL_VERSION(&wmInfo.version);
	SDL_GetWMInfo(&wmInfo);

	// make buffer big enuf for 2x/3x/4x mode
	pBuffer = SDL_CreateRGBSurface(SDL_SWSURFACE,
		outScreenWidth, (int)(outScreenHeight * 1.5), 8,	// w, h, depth
		0, 0, 0, 0);	// rgba masks

	// set palette
	for (int i = 0; i < 256; i++)
		{
		sdlColor.r = (colourtable[i] >> 16) & 0xFF;
		sdlColor.g = (colourtable[i] >> 8) & 0xFF;
		sdlColor.b = (colourtable[i]) & 0xFF;
		SDL_SetColors(pBuffer, &sdlColor, i, 1);
		}

	options.videomode = NTSC;

	frameStart = SDL_GetTicks();

	sprintf(s, "Jum52 V%s Win32/SDL", JUM52_VERSION);
	SDL_WM_SetCaption(s, NULL);
	SDL_WM_SetIcon(SDL_LoadBMP("icon_fuji.bmp"), NULL);

	// hide the cursor and grab the input
	//SDL_ShowCursor(SDL_DISABLE);
	//SDL_WM_GrabInput(SDL_GRAB_ON);

	//g_nWhichBuffer = 0;

	if (options.videomode == NTSC)
		HostLog("Video mode: NTSC\n");
	else
		HostLog("Video mode: PAL\n");

	Wait(10);

	// set up audio output
	reqSpec.freq = SND_RATE;
	reqSpec.format = AUDIO_S8; // AUDIO_U8 in Win32_DSL version
	reqSpec.channels = 1;
	reqSpec.silence = 128;
	if (options.videomode == NTSC)
		reqSpec.samples = 735;
	else
		reqSpec.samples = 882;		// PAL
	reqSpec.callback = fillsoundbuffer;
	reqSpec.userdata = NULL;
	usedSpec = &givenSpec;
	// Open the audio device 
	if (SDL_OpenAudio(&reqSpec, usedSpec) < 0)
		{
		sprintf(s, "Couldn't open audio: %s\n", SDL_GetError());
		HostLog(s);
		fprintf(stderr, s);
		//exit(-1);
		options.audio = 0;
		}

#ifdef _DEBUG
	DebugPrint("SDL audio sample buffer size: %d\n", usedSpec->samples);
#endif

	if (usedSpec == NULL)
		usedSpec = &reqSpec;

	// set up joystick
	if (SDL_NumJoysticks() > 0)
		{
		// Open joystick
		pJoystick = SDL_JoystickOpen(0);
		if (pJoystick)
			{
			HostLog("Opened Joystick 0\n");
			sprintf(s, "Name: %s\n", SDL_JoystickName(0));
			HostLog(s);
			sprintf(s, "Number of Axes: %d\n", SDL_JoystickNumAxes(pJoystick));
			HostLog(s);
			sprintf(s, "Number of Buttons: %d\n", SDL_JoystickNumButtons(pJoystick));
			HostLog(s);
			sprintf(s, "Number of Balls: %d\n", SDL_JoystickNumBalls(pJoystick));
			HostLog(s);
			// turn on event processing
			SDL_JoystickEventState(SDL_ENABLE);
			}
		else
			HostLog("Couldn't open Joystick 0\n");

		// Close if opened
		//if(SDL_JoystickOpened(0))
		//	SDL_JoystickClose(pJoystick);
		}

	// allocate space for ROM info
	romdata = (struct ROMdata *)malloc(MAX_ROMS * sizeof(struct ROMdata));
	if (romdata == NULL)
		{
		HostLog("Couldn't allocate memory for rom list!\n");
		return -1;   // quit
		}

	return 0;
}

// copy debug/emu screen buffer to SDL visible screen
void BlitBuffer(int destX, int destY)
{
	int ret;
	SDL_Rect srcRect, destRect;

	srcRect.x = srcRect.y = 0;
	srcRect.w = 320;
	srcRect.h = 240;
	destRect.x = destX;
	destRect.y = destY;
	destRect.w = 320;
	destRect.h = 240;		// ignored
	ret = SDL_BlitSurface(pBuffer, &srcRect, pSurface, &destRect);
	if (ret < 0)
		fprintf(stderr, "BlitSurface error: %s\n", SDL_GetError());

	SDL_UpdateRect(pSurface, destX, destY, 320, 240);
}


// *********************** DRIVER INTERFACE ***************************
void HostLog(char *s)
{
	if (logfile)
		fprintf(logfile, s);
}

// Prepare host system for setting the palette
void HostPrepareForPaletteSet(void)
{
}

// Set palette entries - assumes 0-255 for each entry
void HostSetPaletteEntry(uint8 entry, uint8 red, uint8 green, uint8 blue)
{
	SDL_Color sdlColor;

	sdlColor.r = red;
	sdlColor.g = green;
	sdlColor.b = blue;
	//    SetPaletteEntry(col, entry);
	SDL_SetColors(pBuffer, &sdlColor, entry, 1);
}

// Required for all the previous changes to take effect
void HostRefreshPalette(void)
{
}

// Actual video copy
void HostBlitVideo(void)
{
	uint8 *pScr;
	uint8 *pLine;
	int x, y;
	SDL_Rect srcRect, destRect;
	char s[256];
	uint32 now;
	int n;

	//lock the surface
	SDL_LockSurface(pBuffer);

	// copy 5200 buffer (gfxdest) to SDL buffer
	// NB: only copy 320x240 from gfxdest to buffer
	pScr = (uint8 *)pBuffer->pixels;
	if (4 == options.scale && !options.debugmode)
		{
		// 4x copy
		for (y = 0; y < 240; y++)
			{
			for (n = 0; n < 4; n++)
				{
				pLine = vid + ((y + 8) * VID_WIDTH);  // skip top 8 lines
				if (3 == n && options.scanlines > 0)
					{
					// JH 2018-10-23 - Handle "scanlines" option - draw every 2nd copy of the line dimmer
					const int shift = min(options.scanlines, 3);
					for (x = 0; x < 320; x++)
						{
						uint8 p = *pLine++;
						p = (p & 0xF0) | ((p & 0x0F) >> shift);
						*pScr++ = p; *pScr++ = p; *pScr++ = p; *pScr++ = p;
						}
					}
				else
					{
					for (x = 0; x < 320; x++)
						{
						uint8 p = *pLine++;
						*pScr++ = p; *pScr++ = p; *pScr++ = p; *pScr++ = p;
						}
					}
				}
			}
		}
	else if (3 == options.scale && !options.debugmode)
		{
		// 3x copy
		for (y = 0; y < 240; y++)
			{
			for (n = 0; n < 3; n++)
				{
				pLine = vid + ((y + 8) * VID_WIDTH);  // skip top 8 lines
				if (2 == n && options.scanlines > 0)
					{
					// JH 2018-10-23 - Handle "scanlines" option - draw every 2nd copy of the line dimmer
					const int shift = min(options.scanlines, 3);
					for (x = 0; x < 320; x++)
						{
						uint8 p = *pLine++;
						p = (p & 0xF0) | ((p & 0x0F) >> shift);
						*pScr++ = p; *pScr++ = p; *pScr++ = p;
						}
					}
				else
					{
					for (x = 0; x < 320; x++)
						{
						uint8 p = *pLine++;
						*pScr++ = p; *pScr++ = p; *pScr++ = p;
						}
					}
				}
			// Draw third (dimmed) copy of this line
			}
		}
	else if (2 == options.scale && !options.debugmode)
		{
		// 2x copy
		for (y = 0; y < 240; y++)
			{
			for (n = 0; n < 2; n++)
				{
				pLine = vid + ((y + 8) * VID_WIDTH);  // skip top 8 lines
				if (1 == n && options.scanlines > 0)
					{
					// JH 2018-10-23 - Handle "scanlines" option - draw every 2nd copy of the line dimmer
					const int shift = min(options.scanlines, 3);
					for (x = 0; x < 320; x++)
						{
						uint8 p = *pLine++;
						p = (p & 0xF0) | ((p & 0x0F) >> shift);
						*pScr++ = p; *pScr++ = p;
						}
					}
				else
					{
					for (x = 0; x < 320; x++)
						{
						*pScr++ = *pLine; *pScr++ = *pLine++;
						}
					}
				}
			}
		}
	else
		{
		// 1x copy
		for (y = 0; y < 240; y++)
			{
			pLine = vid + ((y + 8) * VID_WIDTH);  // skip top 8 lines
			for (x = 0; x < 320; x++)
				{
				*pScr++ = *pLine++;
				}
			// correct pitch if in debugger mode (screen twice as wide)
			if (1 == options.debugmode)
				pScr += 320;
			}
		}

#if defined(VOICE_DEBUG)
	if (1 == options.debugmode)
		{
		sprintf(s, "%d se's", numSampleEvents[0]);
		printXY(s, 8, 0, 0x0F);

		//for(x=0; x<320; x++)
		//	vline(pBuffer, x, 128, 255 - snd[x*3], 0x0f);
		if (gAudioStream)
			{
			hline(pBuffer, 0, 319, 128, 0x0a);
			for (x = 0; x < 735; x++)
				vline(pBuffer, x >> 1, 128, 255 - gAudioStream[x], 0x0f);
			}
		}
#endif

	if (fps_on)
		{
		sprintf(s, "%d fps", fps);
		printXY(s, 2, 2, 0x0F);
		}

	// sync, update and flip buffers here
	while (frameStart >= SDL_GetTicks())
		{
		// give other threads some time
		SDL_Delay(1);
		}
	now = SDL_GetTicks();

	// if a second has gone by
	if (now - lastSecondTick > 1000)
		{
		lastSecondTick = now;
		fps = jum52.m_framesdrawn - lastSecondFrame;
		lastSecondFrame = jum52.m_framesdrawn;
		}

	//unlock surface
	SDL_UnlockSurface(pBuffer);

	// debug output to show how slow we are
	//	if(frameStart < SDL_GetTicks()) {
	//		sprintf(s, "Jum52_SDL - %d ms behind", SDL_GetTicks()-frameStart);
	//		SDL_WM_SetCaption(s, NULL);
	//	}

	if (options.videomode == NTSC)
		frameStart = now + 16;
	else
		frameStart = now + 20;

	// blit 1x/2x/3x/4x
	if (options.scale > 1 && !options.debugmode)
		{
		// 2x mode
		srcRect.x = srcRect.y = 0;
		srcRect.w = outScreenWidth;
		srcRect.h = outScreenHeight;
		destRect.x = destRect.y = 0;
		destRect.w = outScreenWidth;		// ignored
		destRect.h = outScreenHeight;		// ignored
		int ret = SDL_BlitSurface(pBuffer, &srcRect, pSurface, &destRect);
		if (ret < 0)
			fprintf(stderr, "BlitSurface error: %s\n", SDL_GetError());
		}
	else
		{
		// 1x
		BlitBuffer(0, 0);
		}

	SDL_UpdateRect(pSurface, 0, 0, outScreenWidth, outScreenHeight);

	//  SDL_WM_SetCaption("Jum52_SDL", NULL);
}

// cook analog joystick reading to return 5200 pot range
static uint8 cook_joypos(short value)
{
	int val_out;
	int pot_range, pot_range_left, pot_range_right;
	int div_factor;

	pot_range_left = POT_CENTRE - jum52.pot_max_left;
	pot_range_right = jum52.pot_max_right - POT_CENTRE;
	pot_range = pot_range_left + pot_range_right;

	div_factor = 65536 / pot_range;

	val_out = jum52.pot_max_left + ((value + 32768) / div_factor);

	return (uint8)val_out;
}

// Check key status
// Also, allow OS to grab CPU to process it's own events here if neccesary
void HostDoEvents(void)
{
	CController &cont1 = jum52.controller1;
	CController &cont2 = jum52.controller2;

	// use analog mode for joystick and mouse
	cont1.mode = CONT_MODE_DIGITAL;
	cont2.mode = CONT_MODE_DIGITAL;
	if (options.controller > 0)
		{
		cont1.mode = CONT_MODE_ANALOG;
		cont2.mode = CONT_MODE_ANALOG;		// Uncommented JH 2018-09-25
		}

	// get mouse movement
	int mouse_dx, mouse_dy;
	if (2 == options.controller)
		{
		SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
		cont1.analog_h = POT_CENTRE + mouse_dx / 2;
		cont1.analog_v = POT_CENTRE + mouse_dy / 2;
		}

	// get mousepaddle (emulate paddle with mouse) movement
	if (3 == options.controller)
		{
		SDL_GetRelativeMouseState(&mouse_dx, &mouse_dy);
		cont1.analog_h += mouse_dx / 3;
		if (cont1.analog_h < POT_LEFT)
			cont1.analog_h = POT_LEFT;
		if (cont1.analog_h > POT_RIGHT)
			cont1.analog_h = POT_RIGHT;
		}

	// 2016-06-18 - Reset 5200 keypad input
	for (int i = 0; i < 16; i++)
		{
		cont1.keys[i] = 0;
		cont2.keys[i] = 0;
		}

	// read keyboard and fill cont1 & cont2 structures
	unsigned short mappedKeyCode = 0;
	SDL_Event event;
	while (SDL_PollEvent(&event))
		{
		switch (event.type)
			{
				case SDL_QUIT:
					exit_app = 1;
					jum52.m_running = 0;
					break;


				case SDL_JOYHATMOTION:					// D-PAD ?
					if (1 == options.controller)
						{
#ifdef _DEBUG
						//DebugPrint("Joyhatmotion: device = %d, axis = %d\n", event.jhat.which, event.jhat.hat);
#endif
						if (0 == event.jhat.which)
							{
							// Hat position values:
							// SDL_HAT_LEFTUP   SDL_HAT_UP       SDL_HAT_RIGHTUP
							// SDL_HAT_LEFT     SDL_HAT_CENTERED SDL_HAT_RIGHT
							// SDL_HAT_LEFTDOWN SDL_HAT_DOWN     SDL_HAT_RIGHTDOWN
							// Note that zero means the POV is centered.

							Uint8 value = event.jhat.value;
							switch (value)
								{
									case SDL_HAT_LEFT:
										cont1.analog_h = jum52.pot_max_left;
										cont1.analog_v = POT_CENTRE;
										break;
									case SDL_HAT_LEFTUP:
										cont1.analog_h = jum52.pot_max_left;
										cont1.analog_v = jum52.pot_max_left;
										break;
									case SDL_HAT_UP:
										cont1.analog_h = POT_CENTRE;
										cont1.analog_v = jum52.pot_max_left;
										break;
									case SDL_HAT_RIGHTUP:
										cont1.analog_h = jum52.pot_max_right;
										cont1.analog_v = jum52.pot_max_left;
										break;
									case SDL_HAT_RIGHT:
										cont1.analog_h = jum52.pot_max_right;
										cont1.analog_v = POT_CENTRE;
										break;
									case SDL_HAT_RIGHTDOWN:
										cont1.analog_h = jum52.pot_max_right;
										cont1.analog_v = jum52.pot_max_right;
										break;
									case SDL_HAT_DOWN:
										cont1.analog_h = POT_CENTRE;
										cont1.analog_v = jum52.pot_max_right;
										break;
									case SDL_HAT_LEFTDOWN:
										cont1.analog_h = jum52.pot_max_left;
										cont1.analog_v = jum52.pot_max_right;
										break;
									default:
										cont1.analog_h = POT_CENTRE;
										cont1.analog_v = POT_CENTRE;
								}
							}
						}
					break;
				case SDL_JOYAXISMOTION:
					if (1 == options.controller)
						{
#ifdef _DEBUG
						DebugPrint("Joyaxismotion: device = %d, axis = %d\n", event.jaxis.which, event.jaxis.axis);
#endif
						if (0 == event.jaxis.which)
							{
							Uint8 axis = event.jaxis.axis;
							Sint16 value = event.jaxis.value;
							if (0 == axis)
								cont1.analog_h = cook_joypos(value);
							else if (1 == axis)
								cont1.analog_v = cook_joypos(value);
							// Dual-stick support added JH 2018-09-25
							else if (2 == axis)
								cont2.analog_h = cook_joypos(value);
							else if (3 == axis)
								cont2.analog_v = cook_joypos(value);
							}
						}
					break;
				case SDL_JOYBUTTONDOWN:
					//HostLog(s);
#ifdef _DEBUG
					//DebugPrint("Joybutton: device = %d, button = %d\n", event.jbutton.which, event.jbutton.button);
#endif
					if (0 == event.jbutton.which)
						{
						if (0 == event.jbutton.button)
							cont1.trig = 1;
						else if (1 == event.jbutton.button)
							cont1.side_button = 1;
						}
					break;
				case SDL_JOYBUTTONUP:
					if (0 == event.jbutton.which)
						{
						if (0 == event.jbutton.button)
							cont1.trig = 0;
						else if (1 == event.jbutton.button)
							cont1.side_button = 0;
						}
					break;

				case SDL_MOUSEBUTTONDOWN:
					if (2 == options.controller || 3 == options.controller)
						{
						if (event.button.button == SDL_BUTTON_LEFT)
							cont1.trig = 1;
						else if (event.button.button == SDL_BUTTON_RIGHT)
							cont1.side_button = 1;
						}
					break;
				case SDL_MOUSEBUTTONUP:
					if (2 == options.controller || 3 == options.controller)
						{
						if (event.button.button == SDL_BUTTON_LEFT)
							cont1.trig = 0;
						else if (event.button.button == SDL_BUTTON_RIGHT)
							cont1.side_button = 0;
						}
					break;

				case SDL_KEYDOWN:
					// Handle key presses. 
					//sprintf(s, "key code: %d\n", event.key.keysym.sym);
					//HostLog(s);
					if (event.key.keysym.sym < KEY_MAP_SIZE)
						mappedKeyCode = keyMap[event.key.keysym.sym];
					else
						mappedKeyCode = event.key.keysym.sym;
					//switch(event.key.keysym.sym) {
					switch (mappedKeyCode)
						{
						// player 1
							case SDLK_LEFT:
								cont1.left = 1;
								break;
							case SDLK_RIGHT:
								cont1.right = 1;
								break;
							case SDLK_UP:
								cont1.up = 1;
								break;
							case SDLK_DOWN:
								cont1.down = 1;
								break;

							case SDLK_RCTRL:
								cont1.trig = 1;					// controller bottom buttons (L & R)
								break;
							case SDLK_RSHIFT:
								// Fix for version 1.1 - "top fire buttons"
								cont1.side_button = 1;
								// set BRK interrupt bit to 0
								jum52.m_irqst &= 0x7F;
								// check irqen and do interrupt if bit 7 set
								if (jum52.m_irqen & 0x80)
									jum52.CPU.DoIRQ();
								break;

								// player 2
							case SDLK_s:
								cont2.left = 1;
								break;
							case SDLK_f:
								cont2.right = 1;
								break;
							case SDLK_e:
								cont2.up = 1;
								break;
							case SDLK_d:
								cont2.down = 1;
								break;

							case SDLK_LCTRL:
								cont2.trig = 1;
								break;
							case SDLK_LSHIFT:
								cont2.side_button = 1;
								// set BRK interrupt bit to 0
								jum52.m_irqst &= 0x7F;
								// check irqen and do interrupt if bit 7 set
								if (jum52.m_irqen & 0x80)
									jum52.CPU.DoIRQ();
								break;

							case SDLK_BACKQUOTE:		// skip ATARI title
								put6502memory(RTC_LO, 253);
								break;
							case SDLK_F1:		// Start button
								cont1.keys[12] = 1;
								break;
							case SDLK_SLASH:	// Start button PL2
								cont2.keys[12] = 1;
								break;
							case SDLK_F2:		// Pause button
							case SDLK_PAUSE:	// Pause button alternate
								cont1.keys[8] = 1;
								break;
							case SDLK_ASTERISK:	// Pause button PL2
								cont2.keys[8] = 1;
								break;
							case SDLK_F3:		// Reset button
								cont1.keys[4] = 1;
								break;
							case SDLK_KP_MINUS:	// Reset button PL2
								cont2.keys[4] = 1;
								break;
							case SDLK_ESCAPE:		// Escape to menu
								jum52.m_running = 0;
								break;
							case SDLK_F4:		// Monitor
								//timerTicks = 20;
								if (options.debugmode == 1)
									monitor();
								break;
							case SDLK_F5:		// Star keys
							case SDLK_MINUS:
								cont1.keys[3] = 1;
								break;
							case SDLK_F6:		// Hash button
							case SDLK_EQUALS:	// Hash alternate
								cont1.keys[1] = 1;
								break;
							case SDLK_DELETE:		// Star key PL2
								cont2.keys[3] = 1;
								break;
							case SDLK_RETURN:	// Hash button PL2
								cont2.keys[1] = 1;
								break;
							case SDLK_F7:		// visualise sound on/off
								//if(!visualise_sound) visualise_sound = 1;
								//else visualise_sound = 0;
								//rest(200);
								break;
							case SDLK_F8:		// choose controller
								//whichcon = (++whichcon % 3);
								//if(whichcon == 0) textout(screen, font, "Controller is now Keyboard.", 4, 4, 15);
								//if(whichcon == 1) textout(screen, font, "Controller is now Joystick.", 4, 4, 15);
								//if(whichcon == 2) textout(screen, font, "Controller is now Mouse.", 4, 4, 15);
								//rest(1000);
								break;
							case SDLK_F11:		// FPS ON/OFF
								if (fps_on)
									fps_on = 0;
								else
									fps_on = 1;
								break;
							case SDLK_F12:		// Save BMP
								SDL_SaveBMP(pBuffer, "snap.bmp");
								break;
							case SDLK_0:
								cont1.keys[2] = 1;
								break;
							case SDLK_1:
								cont1.keys[15] = 1;
								break;
							case SDLK_2:
								cont1.keys[14] = 1;
								break;
							case SDLK_3:
								cont1.keys[13] = 1;
								break;
							case SDLK_4:
								cont1.keys[11] = 1;
								break;
							case SDLK_5:
								cont1.keys[10] = 1;
								break;
							case SDLK_6:
								cont1.keys[9] = 1;
								break;
							case SDLK_7:
								cont1.keys[7] = 1;
								break;
							case SDLK_8:
								cont1.keys[6] = 1;
								break;
							case SDLK_9:
								cont1.keys[5] = 1;
								break;
							default:
								break;
						} // end switch keydown
					break;
				case SDL_KEYUP:
					// Handle key presses. 
					if (event.key.keysym.sym < KEY_MAP_SIZE)
						mappedKeyCode = keyMap[event.key.keysym.sym];
					else
						mappedKeyCode = event.key.keysym.sym;
					//switch(event.key.keysym.sym) {
					switch (mappedKeyCode)
						{
						// player 1
							case SDLK_LEFT:
								cont1.left = 0;
								break;
							case SDLK_RIGHT:
								cont1.right = 0;
								break;
							case SDLK_UP:
								cont1.up = 0;
								break;
							case SDLK_DOWN:
								cont1.down = 0;
								break;

							case SDLK_RCTRL:
								cont1.trig = 0;
								break;
							case SDLK_RSHIFT:
								cont1.side_button = 0;
								break;

								// player 2
							case SDLK_s:
								cont2.left = 0;
								break;
							case SDLK_f:
								cont2.right = 0;
								break;
							case SDLK_e:
								cont2.up = 0;
								break;
							case SDLK_d:
								cont2.down = 0;
								break;

							case SDLK_LCTRL:
								cont2.trig = 0;
								break;
							case SDLK_LSHIFT:
								cont2.side_button = 0;
								break;

							case SDLK_F1:		// Start button
								cont1.keys[12] = 0;
								break;
							case SDLK_SLASH:	// Start button PL2
								cont2.keys[12] = 0;
								break;
							case SDLK_F2:		// Pause button
							case SDLK_PAUSE:	// Pause button alternate
								cont1.keys[8] = 0;
								break;
							case SDLK_ASTERISK:	// Pause button PL2
								cont2.keys[8] = 0;
								break;
							case SDLK_F3:		// Reset button
								cont1.keys[4] = 0;
								break;
							case SDLK_KP_MINUS:	// Reset button PL2
								cont2.keys[4] = 0;
								break;
							case SDLK_F5:		// Star key
							case SDLK_MINUS:
								cont1.keys[3] = 0;
								break;
							case SDLK_F6:		// Hash button
							case SDLK_EQUALS:	// Hash alternate
								cont1.keys[1] = 0;
								break;
							case SDLK_DELETE:		// Star key PL2
								cont2.keys[3] = 0;
								break;
							case SDLK_RETURN:	// Hash button PL2
								cont2.keys[3] = 0;
								break;
							case SDLK_0:
								cont1.keys[2] = 0;
								break;
							case SDLK_1:
								cont1.keys[15] = 0;
								break;
							case SDLK_2:
								cont1.keys[14] = 0;
								break;
							case SDLK_3:
								cont1.keys[13] = 0;
								break;
							case SDLK_4:
								cont1.keys[11] = 0;
								break;
							case SDLK_5:
								cont1.keys[10] = 0;
								break;
							case SDLK_6:
								cont1.keys[9] = 0;
								break;
							case SDLK_7:
								cont1.keys[7] = 0;
								break;
							case SDLK_8:
								cont1.keys[6] = 0;
								break;
							case SDLK_9:
								cont1.keys[5] = 0;
								break;
							default:
								break;
						} //end switch keyup
					break;
			} // end outer switch
		}

	// bypass SELECT menu if user issued SDL_QUIT
	if (1 == exit_app)
		return;

	/*
			// SELECT menu
			if(!running) {
			selected = DoOptionsMenu();
			running = 1;
			switch(selected) {
			case 0: // Load Game
			running = 0;
			break;
			case 1: // Save State
			break;
			case 2: // Load State
			break;
			case 6: // Reset 5200
			Jum52_Reset();
			break;
			case 7: // return
			exit_app = 1;
			running = 0;
			break;
			}
			}
			*/
}

// Interrupts  (not used in Win32 version)
void HostEnableInterrupts(void)
{
}

void HostDisableInterrupts(void)
{
}

// Sound output
void HostProcessSoundBuffer(void)
{
	// Avoid a hang
	if (0 == options.audio)
		return;

	// 2017-01-03 wait until clear to fill the sound buffer
	// (ie: until fillsoundbuffer() has been run)
	// this will sync the audio output, and sync the emu to the audio output)
	while (0 == fillSoundBufferDone)
		{
		Sleep(1);
		}

	// update buffer with new data
	if (options.audio)
		jum52.pokey.Process(snd, snd_buf_size);

	// render "voice" buffer if necessary
	if (options.voice)
		renderMixSampleEvents(snd, snd_buf_size);

	fillSoundBufferDone = 0;
}

// Load 5200 emu state
int HostLoadState(char *filename)
{
	FILE *pfile = fopen( filename, "rb" );
	if (pfile == NULL)
		{
		sprintf(errormsg, "Unable to find state file to load.");
		return -1;
		}
		
	// Load state from disk
	STATE_5200 state;
	fread(&state, sizeof(STATE_5200), 1, pfile);

	// copy RAM in
	fread(memory5200, 0x10000, 1, pfile);

	fclose(pfile);

	// Restore CPU state
	jum52.CPU.A = state.A;
	jum52.CPU.X = state.X;
	jum52.CPU.Y = state.Y;
	jum52.CPU.P = state.F;
	jum52.CPU.S = state.S;
	jum52.CPU.PC = state.PC;
	jum52.m_irqen = state.IrqEn;
	jum52.m_irqst = state.IrqSt;

	return 0;
}

// Save 5200 emu state
int HostSaveState(char *filename)
{
	FILE *pfile = fopen( filename, "wb" );
	if (pfile == NULL)
		{
		sprintf(errormsg, "Unable to open state file for saving.");
		return -1;
		}

	// set state
	STATE_5200 state;
	state.A = jum52.CPU.A;
	state.X = jum52.CPU.X;
	state.Y = jum52.CPU.Y;
	state.F = jum52.CPU.P;
	state.S = jum52.CPU.S;
	state.PC = jum52.CPU.PC;
	state.IrqEn = jum52.m_irqen;
	state.IrqSt = jum52.m_irqst;

	// save state to disk
	fwrite(&state, sizeof(STATE_5200), 1, pfile);

	// save RAM
	fwrite(memory5200, 0x10000, 1, pfile);

	fclose(pfile);

	return 0;
}

// ************************* END OF DRIVER INTERFACE **************************



// Clear emulation screen buffer
void clrEmuScreen(unsigned char colour)
{
	unsigned char *pp = (unsigned char *)pBuffer->pixels;
	unsigned int numbytes = pBuffer->w * pBuffer->h;
	while (numbytes--)
		*pp++ = colour;

}

// Draw a box on the emulation screen buffer
static void drawBox(int x1, int y1, int x2, int y2, unsigned char colour)
{
	for (int y = y1; y <= y2; y++)
		{
		unsigned char *pp = (unsigned char*)pBuffer->pixels + y * pBuffer->pitch + x1;
		for (int x = x1; x <= x2; x++)
			{
			*pp++ = colour;
			}
		}
}

// draw a char on the output buffer using the system font
static void drawChar(char c, int x, int y, unsigned int colour)
{
	// set character pointer
	unsigned char *pc = &font5200[(c - 32) * 8];

	// set screen pointer
	unsigned char *pp = (unsigned char*)pBuffer->pixels + y * pBuffer->pitch + x;
	for (unsigned int i = 0; i < 8; i++)
		{
		unsigned char cc = *pc++;
		for (unsigned int j = 0; j < 8; j++)
			{
			if (cc & 0x80)
				*pp = colour;
			cc = cc << 1;
			pp++;
			}
		pp += (pBuffer->pitch - 8);
		}
}

// draw a string of characters
void printXY(char *s, int x, int y, unsigned int colour)
{
	while (*s)
		{
		drawChar(*s++, x, y, colour);
		x += 8;
		}
}

/* UNUSED 
// Windows open file dialog to browse for rom
char *doFileBrowseDialog(void)
{
	//OPENFILENAME ofn;       // common dialog box structure
	OPENFILENAMEA ofn;
	char szFile[260];       // buffer for file name
	BOOL ret;
	//HWND hwnd;              // owner window

	SDL_WM_GrabInput(SDL_GRAB_OFF);

	strcpy(szFile, "");

	// Initialize OPENFILENAME
	ZeroMemory(&ofn, sizeof(OPENFILENAME));
	ofn.lStructSize = sizeof(OPENFILENAME);
	ofn.hwndOwner = wmInfo.window; //hwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = 260;
	ofn.lpstrFilter = "Atari 5200 roms\0*.BIN;*.A52\0All\0*.*\0\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	// Display the Open dialog box.
	ret = GetOpenFileNameA(&ofn);

	SDL_WM_GrabInput(SDL_GRAB_ON);

	if (ret != FALSE)
		{
		strcpy(rompath, szFile);
		return rompath;
		}

	return NULL;
}
*/

// ROM selection menu (new version)
static char *DoRomMenu()
{
	char s[256];
	int debounce = 0;
	SDL_Event event;
	long findHandle;
	int escaped = 0;

	struct _finddata_t fileinfo;

	//return doFileBrowseDialog();

	// get a list of .bin files in the rom directory
	num_roms = 0;

	// Find *.*, then filter by extensions .bin, .a52, .rom
	findHandle = _findfirst("*.*", &fileinfo);
	if (-1 == findHandle)
		return NULL;	// no roms found!

	do
		{
		if (strlen(fileinfo.name) > 4)
			{
			if (strstr(fileinfo.name, ".bin") || strstr(fileinfo.name, ".BIN") ||
				strstr(fileinfo.name, ".a52") || strstr(fileinfo.name, ".A52") ||
				strstr(fileinfo.name, ".rom") || strstr(fileinfo.name, ".ROM"))
				{
				strcpy(romdata[num_roms++].name, fileinfo.name);
				}
			}
		} while (0 == _findnext(findHandle, &fileinfo) && num_roms < MAX_ROMS);

	_findclose(findHandle);

#ifdef _DEBUG
	for (int i = 0; i < num_roms; i++)
		{
		sprintf(s, "rom: %s\n", romdata[i].name);
		HostLog(s);
		}
#endif

	// No roms in the folder?
	if (0 == num_roms)
		{
		HostLog("No roms in current folder!");
		return NULL;
		}

	// choose rom from list
	while (1)
		{
		selection = frame_position + 9;            // rom in middle of frame is always the selected one
		int key = 0;

		// get key (if any)
		if (SDL_PollEvent(&event))
			{
			switch (event.type)
				{
					case SDL_KEYDOWN:
						key = event.key.keysym.sym;
						break;
					case SDL_QUIT:
						exit_app = 1;
						jum52.m_running = 0;
						break;
				}
			}

		// check if ROM selected
		if (key == SDLK_SPACE || key == SDLK_RETURN)
			{
			break;
			}

		// check other keys
		// move ROM selector up/down
		if (key == SDLK_DOWN)
			{
			if (frame_position < (num_roms - 10))
				frame_position++;
			//debounce = DEBOUNCE_RATE;
			}
		if (key == SDLK_UP)
			{
			if (frame_position > -9)
				frame_position--;
			//debounce = DEBOUNCE_RATE;
			}
		// page ROM selector up/down
		if (key == SDLK_PAGEUP)
			{
			frame_position -= 10;
			if (frame_position < -9)
				frame_position = -9;
			//debounce = DEBOUNCE_RATE;
			}
		if (key == SDLK_PAGEDOWN)
			{
			frame_position += 10;
			if (frame_position >(num_roms - 1))
				frame_position = (num_roms - 1);
			//debounce = DEBOUNCE_RATE;
			}

		if (key == SDLK_ESCAPE)
			{
			escaped = 1;
			break;
			}

		// clear draw buffer
		clrEmuScreen(0x00);
		drawBox(4, 4, 315, 235, 0x50);
		sprintf(s, "Jum's Atari 5200 Emulator V%s", JUM52_VERSION);
		printXY(s, 32, 8, 0x4E);
		printXY("by James Higgs 2000-2018", 52, 16, 0x44);

		// Draw text in scroll box
		const int ROM_INDENT = 24;
		int y = 32;
		for (int i = frame_position; i < (frame_position + 19); i++)
			{
			if (i >= 0 && i < num_roms)
				{
				if (i == selection)
					{
					printXY(romdata[i].name, ROM_INDENT, y, 0x0f);
					printXY(">", ROM_INDENT - 8, y, 0x0f);
					}
				else
					printXY(romdata[i].name, ROM_INDENT, y, 0x18);
				}
			y += 8;
			}

		printXY("Press [Space] to select a game.", 32, 220, 0x1C);

		hline(pBuffer, 4, 315, 4, 0x0F);
		hline(pBuffer, 4, 315, 235, 0x0F);
		vline(pBuffer, 4, 4, 235, 0x0F);
		vline(pBuffer, 315, 4, 235, 0x0F);

		//printXY(getRomFullPath(selection), 10, 212, 0x07);

		//if(debounce) debounce--;					// update key delay if neccessary

		while (frameStart > SDL_GetTicks())
			{
			// do idle loop stuff here
			SDL_Delay(20);
			}
		frameStart = SDL_GetTicks() + 20;

		//SDL_BlitSurface(pBuffer, NULL, pSurface, NULL);
		//SDL_UpdateRect(pSurface, 0, 0, 320, 240);

		// TODO - Scale to full window
		SDL_Rect destRect;
		destRect.x = (outScreenWidth - 320) / 2;
		destRect.y = (outScreenHeight - 240) / 2;
		destRect.w = 320;
		destRect.h = 240;
		SDL_BlitSurface(pBuffer, NULL, pSurface, &destRect);
		SDL_UpdateRect(pSurface, 0, 0, outScreenWidth, outScreenHeight);
		}

	currentromindex = selection;

	// is user quitting or escaped?
	if (1 == exit_app)
		return NULL;

	// Did user escape?
	if (1 == escaped)
		return "esc";

	// return the full path name of this rom file
	//return getRomFullPath(selection);

	return romdata[selection].name;
}

// Help screen
static void DoHelpMenu()
{
	SDL_Event event;

	// clear draw buffer
	clrEmuScreen(0x00);
	drawBox(4, 4, 315, 235, 0x50);
	printXY("Help for Controls", 60, 8, 0x4E);

	printXY("ESC             Options Menu", 16, 24, 0x18);
	printXY("F1              5200 Start Button", 16, 32, 0x18);
	printXY("F2              5200 Pause Button", 16, 40, 0x18);
	printXY("F3              5200 Reset Button", 16, 48, 0x18);
	printXY("Arrow Keys      5200 Joystick", 16, 56, 0x18);
	printXY("Right Ctrl      5200 Fire 1", 16, 64, 0x18);
	printXY("Right Shift     5200 Fire 2", 16, 72, 0x18);
	printXY("F5              5200 * Button", 16, 80, 0x18);
	printXY("F6              5200 # Button", 16, 88, 0x18);
	printXY("E/S/D/F         Player 2 Joystick", 16, 96, 0x18);
	printXY("Left Ctrl       Player 2 Fire 1", 16, 104, 0x18);
	printXY("Left Shift      Player 2 Fire 2", 16, 112, 0x18);
	printXY("F11             Show speed (FPS)", 16, 120, 0x18);
	printXY("F12             Take a snapshot", 16, 128, 0x18);

	printXY("Press [Esc] to exit.", 32, 212, 0x1C);

	SDL_BlitSurface(pBuffer, NULL, pSurface, NULL);
	SDL_UpdateRect(pSurface, 0, 0, 320, 240);

	// Display help screen
	while (1)
		{
		int key = 0;

		// get key (if any)
		if (SDL_PollEvent(&event))
			{
			switch (event.type)
				{
					case SDL_KEYDOWN:
						key = event.key.keysym.sym;
						break;
					case SDL_QUIT:
						exit_app = 1;
						return;
				}
			}

		// check if key pressed
		if (0 != key)
			break;

		SDL_Delay(20);
		}
}

// SELECT menu
// 0. Resume game
// 1. Load game
// 2. Save Game State
// 3. Load Game State
// 4. Controller: K/M/J
// 5. Control Mode: Normal/Robotron/Pengo
// 6. Volume: 0 to 100%
// 7. Reset 5200
// 8. Quit Jum52
#define SELECT_ROW_MAX     8
static int DoOptionsMenu(void)
{
	int i, selected_row;
	int debounce = 0;
	char s[256];
	unsigned char c;
	char msg[32] = "";
	SDL_Event event;

	if (debugging)
		return -1;

	selected_row = 0;
	while (1)
		{
		int key = 0;

		// get key (if any)
		if (SDL_PollEvent(&event))
			{
			switch (event.type)
				{
				case SDL_KEYDOWN:
					key = event.key.keysym.sym;
					break;
				case SDL_QUIT:
					key = SDLK_q;		// force quit
					break;
				}
			}

		if (key == SDLK_q)
			{
			selected_row = 8;
			break;
			}

		if (key == SDLK_F1)
			{
			DoHelpMenu();
			Wait(20);
			}

		// check for button press
		if (key == SDLK_SPACE || key == SDLK_RETURN)
			{ // SPACE/RETURN
			if (0 == selected_row)
				break; // resume game
			else if (1 == selected_row)
				break;	// load game
			else if (2 == selected_row)
				{
				HostSaveState("state");
				strcpy(msg, "Game state saved.");
				Wait(30);
				}
			else if (3 == selected_row)
				{
				HostLoadState("state");
				strcpy(msg, "Game state loaded.");
				Wait(30);
				}
			else if (7 == selected_row)
				{
				jum52.Reset();
				strcpy(msg, "5200 reset.");
				Wait(30);
				}
			else if (8 == selected_row)
				break; // quit
			}

		// return to game?
		if (key == SDLK_ESCAPE)
			{
			selected_row = 0;
			Wait(30);
			break;
			}

		// move ROM selector up/down
		if (key == SDLK_DOWN)
			if (selected_row < SELECT_ROW_MAX)
				selected_row++;

		if (key == SDLK_UP)
			if (selected_row > 0)
				selected_row--;

		if ((key == SDLK_RIGHT) || (key == SDLK_LEFT))
			{
			switch (selected_row)
				{
					case 4: // controller
						options.controller++;
						if (4 == options.controller)
							options.controller = 0;
						break;
					case 5: // control mode
						options.controlmode++;
						if (3 == options.controlmode)
							options.controlmode = 0;
						break;
					case 6: // volume
						if (key == SDLK_RIGHT)
							{
							options.volume += 5;
							if (options.volume > 100)
								options.volume = 100;
							}
						else // key == SDLK_LEFT
							{
							options.volume -= 5;
							if (options.volume < 0)
								options.volume = 0;
							}
						break;
				}
			}

		// clear draw buffer
		clrEmuScreen(0x00);
		drawBox(4, 4, 315, 235, 0x50);
		//printXY("Jum52 Options Menu", 80, 8, 0x1f);
		sprintf(s, "Jum's Atari 5200 Emulator V%s", JUM52_VERSION);
		printXY(s, 32, 8, 0x4E);
		printXY("by James Higgs 2000-2018", 52, 16, 0x44);

		// Draw options list
#define INDENT 80
		int y = 48;
		for (i = 0; i <= SELECT_ROW_MAX; i++)
			{
			c = 0x18;
			if (i == selected_row)
				{
				c = 0x0f;
				printXY(">", INDENT - 8, y, c);
				}

			switch (i)
				{
					case 0:
						printXY("Resume Game", INDENT, y, c);
						break;
					case 1:
						printXY("Load Game", INDENT, y, c);
						break;
					case 2:
						printXY("Save State", INDENT, y, c);
						break;
					case 3:
						printXY("Load State", INDENT, y, c);
						break;
					case 4:
						strcpy(s, "Controller: Keyboard");
						if (1 == options.controller)
							strcpy(s, "Controller: Joystick");
						else if (2 == options.controller)
							strcpy(s, "Controller: Mouse");
						else if (3 == options.controller)
							strcpy(s, "Controller: MousePaddle");
						printXY(s, INDENT, y, c);
						break;
					case 5:
						strcpy(s, "Control Mode: Normal");
						if (1 == options.controlmode)
							strcpy(s, "Control Mode: Robotron");
						else if (2 == options.controlmode)
							strcpy(s, "Control Mode: Pengo");
						printXY(s, INDENT, y, c);
						break;
					case 6:
						sprintf(s, "Volume: %d%%", options.volume);
						printXY(s, INDENT, y, c);
						break;
					case 7:
						printXY("Reset 5200", INDENT, y, c);
						break;
					case 8: // Quit
						printXY("Quit Jum52", INDENT, y, c);
						break;
					default:
						printXY("Oops!", INDENT, y, c);
				}
			y += 12;
			}

		printXY(msg, 32, 160, 0x3f);
		printXY("Press [Esc] to return to game.", 32, 204, 0x1f);
		printXY("Press [F1] for help.", 32, 212, 0x1f);
		printXY("Press [Q] to Quit.", 32, 220, 0x1f);
		hline(pBuffer, 4, 315, 4, 0x0F);
		hline(pBuffer, 4, 315, 235, 0x0F);
		vline(pBuffer, 4, 4, 235, 0x0F);
		vline(pBuffer, 315, 4, 235, 0x0F);

		while (frameStart > SDL_GetTicks())
			{
			// do idle loop stuff here
			SDL_Delay(20);
			}
		frameStart = SDL_GetTicks() + 20;

		// TODO - Scale to full window
		SDL_Rect destRect;
		destRect.x = (outScreenWidth - 320) / 2;
		destRect.y = (outScreenHeight - 240) / 2;
		destRect.w = 320;
		destRect.h = 240;
		SDL_BlitSurface(pBuffer, NULL, pSurface, &destRect);
		SDL_UpdateRect(pSurface, 0, 0, outScreenWidth, outScreenHeight);
		}	// wend

	return selected_row;
}



//////////////////////////////////////////////////////////////////////////////////////////
// DEBUGGER/MONITOR FUNCTIONS
//////////////////////////////////////////////////////////////////////////////////////////

// In debugger.cpp
extern void printhelp(int x, int y);
extern void hexview(uint16 viewaddr);
extern void listvectors();
extern unsigned int disassemble(uint16 addr1, uint16 addr2);
extern void sprintbin(char *s, uint8 byte);
extern uint16 getaddr(uint16 memaddr);
extern void showcollisionregs();

// Display PM gfx in debug mode (used by debugger)
void showPMdebug(void)
{
	int i, n, x, pmbase;
	uint8 data;

	// show pm gfx
	clrEmuScreen(0x00);

	// single-line mode ?
	if (memory5200[DMACTL] & 0x10)
		{
		pmbase = (memory5200[PMBASE] & 0xF8) << 8;
		for (i = 0; i < 256; i++)
			{
			for (n = 0; n < 5; n++)
				{
				x = n * 64;
				data = memory5200[pmbase + n * 256 + 768 + i];
				if (data & 0x80) SDL_SetPixel(pBuffer, x, i, 15);
				if (data & 0x40) SDL_SetPixel(pBuffer, x + 1, i, 15);
				if (data & 0x20) SDL_SetPixel(pBuffer, x + 2, i, 15);
				if (data & 0x10) SDL_SetPixel(pBuffer, x + 3, i, 15);
				if (data & 0x08) SDL_SetPixel(pBuffer, x + 4, i, 15);
				if (data & 0x04) SDL_SetPixel(pBuffer, x + 5, i, 15);
				if (data & 0x02) SDL_SetPixel(pBuffer, x + 6, i, 15);
				if (data & 0x01) SDL_SetPixel(pBuffer, x + 7, i, 15);
				}
			}
		}
	else
		{	// double-line mode
		pmbase = (memory5200[PMBASE] & 0xFC) << 8;
		for (i = 0; i < 256; i++)
			{
			for (n = 0; n < 5; n++)
				{
				x = n * 64;
				data = memory5200[pmbase + n * 128 + 384 + i / 2];
				if (data & 0x80) SDL_SetPixel(pBuffer, x, i, 15);
				if (data & 0x40) SDL_SetPixel(pBuffer, x + 1, i, 15);
				if (data & 0x20) SDL_SetPixel(pBuffer, x + 2, i, 15);
				if (data & 0x10) SDL_SetPixel(pBuffer, x + 3, i, 15);
				if (data & 0x08) SDL_SetPixel(pBuffer, x + 4, i, 15);
				if (data & 0x04) SDL_SetPixel(pBuffer, x + 5, i, 15);
				if (data & 0x02) SDL_SetPixel(pBuffer, x + 6, i, 15);
				if (data & 0x01) SDL_SetPixel(pBuffer, x + 7, i, 15);
				}
			}
		}

	// print player gfx horizontal offsets
	for (i = 0; i < 4; i++)
		{
		sprintf(msg, "%d", memory5200[HPOSP0 + i]);
		printXY(msg, i * 64 + 74, 0, 255);
		}

	// print missile gfx horizontal offsets
	for (i = 0; i < 4; i++)
		{
		sprintf(msg, "%d", memory5200[HPOSM0 + i]);
		printXY(msg, 10, i * 8, 247);

		}

	BlitBuffer(0, 240);
}

// display character set (used by debugger)
void display_charset(void)
{
	int i, j, k, x, y;

	uint16 chbase = memory5200[CHBASE] << 8;
	//clear(buffer);
	//textprintf(buffer, font, 0, 0, 15, "Character set at 0x%4X", chbase);
	uint8 c = 15;
	for (i = 0; i < 16; i++)
		{
		for (j = 0; j < 16; j++)
			{
			for (k = 0; k < 8; k++)
				{
				uint8 d = memory5200[chbase++];
				x = j * 8; y = i * 8 + k;
				if (d & 0x80) SDL_SetPixel(pBuffer, x, y, c);
				if (d & 0x40) SDL_SetPixel(pBuffer, x + 1, y, c);
				if (d & 0x20) SDL_SetPixel(pBuffer, x + 2, y, c);
				if (d & 0x10) SDL_SetPixel(pBuffer, x + 3, y, c);
				if (d & 0x08) SDL_SetPixel(pBuffer, x + 4, y, c);
				if (d & 0x04) SDL_SetPixel(pBuffer, x + 5, y, c);
				if (d & 0x02) SDL_SetPixel(pBuffer, x + 6, y, c);
				if (d & 0x01) SDL_SetPixel(pBuffer, x + 7, y, c);
				}
			}
		}
	BlitBuffer(0, 240);
}

// allegro gets()  (used by debugger)
static int getstring(char *prompt)
{
	clrEmuScreen(0x00);
	printXY(prompt, 0, 0, 13);
	BlitBuffer(320, 240);

	int i = 0;
	char c = getkey(); //(readkey() & 0xFF);
	while (c != 13)
		{
		//while(!keypressed()) ;
		string[i++] = c;
		sprintf(msg, "%c", c);
		printXY(msg, i * 8, 8, 15);
		BlitBuffer(320, 240);
		c = getkey(); //(readkey() & 0xFF);
		}
	// terminate!
	string[i] = 0;

	return i;
}

// Monitor/debugger
static int monitor(void)
{
	int n = 0;
	uint16 addr;
	unsigned int new_addr;
	char mystring[128];

	if (debugging)
		return 0;

	debugging = 1;

	if (options.audio)
		SDL_PauseAudio(1);

	C6502 &CPU = jum52.CPU;

	printhelp(320, 240);					// In debugger.cpp
	memset(mystring, 0, 128);
	while (strcmp(mystring, "quit") != 0)
		{
		uint16 rtc5200 = (memory5200[RTC_HI] << 8) + memory5200[RTC_LO];
		int vcount = get6502memory(VCOUNT) * 2;

		// draw registers
		clrEmuScreen(0x00);
		sprintf(msg, "A %2X  X %2X  Y %2X  S %2X  PC %4X NVGBDIZC\n", CPU.A, CPU.X, CPU.Y, CPU.S, CPU.PC);
		printXY(msg, 0, 0, 15);
		sprintbin(mystring, CPU.P);
		sprintf(msg, "                                %s", mystring);
		printXY(msg, 0, 8, 15);
		sprintf(msg, "RTC %4X  ATR %2X", rtc5200, memory5200[ATTRACT_TIMER]);
		printXY(msg, 0, 16, 15);
		sprintbin(mystring, jum52.antic.m_nmien);
		sprintf(msg, "NMIEN %s", mystring);
		printXY(msg, 0, 24, 15);
		sprintbin(mystring, jum52.antic.m_nmist);
		sprintf(msg, "NMIST %s", mystring);
		printXY(msg, 0, 32, 15);
		sprintbin(mystring, jum52.m_irqen);
		sprintf(msg, "IRQEN %s", mystring);
		printXY(msg, 160, 24, 15);
		sprintbin(mystring, jum52.m_irqst);
		sprintf(msg, "IRQST %s", mystring);
		printXY(msg, 160, 32, 15);

		// ANTIC REGS
		sprintf(msg, "DLIST %X   CHBASE %X   PMBASE %X", getaddr(DLISTL), memory5200[CHBASE] << 8, memory5200[PMBASE] << 8);
		printXY(msg, 0, 48, 15);
		sprintf(msg, "PFCOL's: %2X  %2X  %2X  %2X", memory5200[COLPF0], memory5200[COLPF1], memory5200[COLPF2], memory5200[COLPF3]);
		printXY(msg, 0, 56, 15);
		sprintf(msg, "PMCOL's: %2X  %2X  %2X  %2X", memory5200[COLPM0], memory5200[COLPM1], memory5200[COLPM2], memory5200[COLPM3]);
		printXY(msg, 0, 64, 15);
		sprintf(msg, "COLBK:   %2X  VCOUNT %X", memory5200[COLBK], vcount / 2);
		printXY(msg, 0, 72, 15);
		sprintbin(mystring, memory5200[HSCROL]);
		sprintf(msg, "HSCROL %s", mystring);
		printXY(msg, 0, 80, 15);
		sprintbin(mystring, memory5200[VSCROL]);
		sprintf(msg, "VSCROL %s", mystring);
		printXY(msg, 160, 80, 15);
		sprintbin(mystring, memory5200[DMACTL]);
		sprintf(msg, "DMACTL %s", mystring);
		printXY(msg, 0, 88, 15);
		sprintbin(mystring, memory5200[CHACTL]);
		sprintf(msg, "CHACTL %s", mystring);
		printXY(msg, 160, 88, 15);
		sprintbin(mystring, memory5200[GRACTL]);
		sprintf(msg, "GRACTL %s", mystring);
		printXY(msg, 0, 96, 15);
		sprintbin(mystring, memory5200[PRIOR]);
		sprintf(msg, "PRIOR  %s", mystring);
		printXY(msg, 160, 96, 15);
		sprintbin(mystring, memory5200[CONSOL]);
		sprintf(msg, "CONSOL %s", mystring);
		printXY(msg, 0, 104, 15);
		sprintf(msg, "P2PL  %d  %d  %d  %d", jum52.P2PL[0], jum52.P2PL[1], jum52.P2PL[2], jum52.P2PL[3]);
		printXY(msg, 0, 112, 15);

		sprintf(msg, "VCOUNT %d", vcount);
		printXY(msg, 0, 120, 15);
		sprintf(msg, "HSYNC  %d", jum52.antic.m_ticksToHSYNC);
		printXY(msg, 0, 128, 15);
		sprintf(msg, "NEXT ML   %d", jum52.antic.m_nextModeLine);
		printXY(msg, 160, 120, 15);
		sprintf(msg, "NEXT DLI  %d", jum52.antic.m_nextDLI);
		printXY(msg, 160, 128, 15);
		sprintf(msg, "dladdr %4X", jum52.antic.m_dladdr);
		printXY(msg, 0, 136, 15);
		sprintf(msg, "scanaddr %4X", jum52.antic.m_scanaddr);
		printXY(msg, 160, 136, 15);
		sprintf(msg, "tickstilldraw %d", jum52.antic.m_ticksTillDraw);
		printXY(msg, 0, 144, 15);
		sprintf(msg, "stolencycles %d",jum52. stolencycles);
		printXY(msg, 160, 144, 15);
		sprintf(msg, "NMI: %d   IRQ: %d  IRQ_PENDING %d", CPU.GetNMIBusy(), CPU.GetIRQBusy(), CPU.GetIRQPending());
		printXY(msg, 0, 160, 15);
		// JH 12/2/2002 - changed line below to display hex value
		sprintf(msg, "KBCODE: 0x%02X", memory5200[KBCODE]);
		printXY(msg, 0, 168, 15);
		sprintf(msg, "%4X> ", CPU.PC);
		printXY(msg, 0, 220, 15);
		BlitBuffer(320, 0);

		// disassemble blits it's output to 0, 240
		disassemble(CPU.PC, 0);		// 20 lines

		// get input
		clrEmuScreen(0x00);
		char ccmd = getkey(); // (readkey() & 0xFF);
		//fprintf(stderr, "key: %d\n", ccmd);
		sprintf(msg, "%c", ccmd);
		printXY(msg, 0, 0, 15);
		//n = sscanf(mystring, "%c %x", &ccmd, &new_addr);
		//printf("DEBUG: cmd: %c  param: %x\n", ccmd, new_addr);
		if (ccmd == 27)
			strcpy(mystring, "quit");
		// single step
		if (ccmd == 's')
			{
			jum52.RunDebug(1);
			HostBlitVideo();
			}
		// hundred step (run 100 cycles)
		if (ccmd == 'S')
			{
			jum52.RunDebug(100);
			HostBlitVideo();
			}
		// run (resume)
		if (ccmd == 'r')
			{
			break;
			//exec6502fast(1000000000);		// arg is ignored
			}
		// run till next frame / VBI
		if (ccmd == 'f')
			{
			jum52.RunDebug(1000);
			//while (vcount != 247)
				//exec6502debug(2);
			while (get6502memory(VCOUNT) * 2 < 248)
				jum52.RunDebug(2);
			}
		// help command
		if (ccmd == 'h')
			{
			printhelp(0, 240);
			getkey();
			}
		// check for v command
		if (ccmd == 'v')
			{
			hexview(CPU.PC);
			getkey();
			}
		// check for v command
		if (ccmd == 'V')
			{
			getstring("Enter address to view:");
			n = sscanf(string, "%x", &new_addr);
			hexview((uint16)new_addr);
			getkey();
			}
		// check for l command (view display list)
		if (ccmd == 'l')
			{
			hexview(getaddr(DLISTL));
			getkey();
			}
		// check for c command (view display list)
		if (ccmd == 'c')
			{
			display_charset();	//getaddr(CHBASE));
			getkey();
			}
		// list int vectors
		if (ccmd == 'i')
			{
			listvectors();
			getkey();
			}
		// quit
		if (ccmd == 'q')
			strcpy(mystring, "quit");

		// RESET
		if (ccmd == 'R')
			{
			// TEST - RESET
			jum52.Reset();
			}

		// run from addr
		if (ccmd == 'J')
			{
			getstring("Enter address to jump to:");
			n = sscanf(string, "%x", &new_addr);
			CPU.PC = new_addr;
			//exec6502debug(new_addr);
			}
		// run To addr (n is watchdog)
		if (ccmd == 'T')
			{
			getstring("Enter address to run to:");
			n = sscanf(string, "%x", &new_addr);
			if (n > 0 && n < 0xFFFF)
				{
				while (CPU.PC != new_addr)
					{
					jum52.RunDebug(1);
					}
				}
			}
		// check for disassemble remote command
		if (ccmd == 'D')
			{
			getstring("Enter address to disassemble:");
			n = sscanf(string, "%x", &new_addr);
			addr = (uint16)new_addr;
			disassemble(addr, (uint16)(addr + 20));
			printXY("Press any key to continue.", 0, 230, 14);
			BlitBuffer(0, 240);
			getkey(); //while(!keypressed());
			}
		// DEBUG - cause KEYBOARD int with START
		if (ccmd == 'K')
			{
			jum52.m_irqst &= 0xbf;
			memory5200[KBCODE] = 0x19;        // (0x0C << 1) | 1;
			if (jum52.m_irqen & 0x40)
				{
				CPU.DoIRQ();
				}
			}
		// DEBUG - cause BREAK int
		if (ccmd == 'B')
			{
			jum52.m_irqst &= 0x7f;
			if (jum52.m_irqen & 0x80)
				{
				CPU.DoIRQ();
				}
			}
		// DEBUG - press TRIG0
		if (ccmd == '0')
			{
			jum52.controller1.trig = 1;
			}

		// show PM gfx
		if (ccmd == 'p')
			{
			showPMdebug();
			getkey();
			}

		// show collision regs
		if (ccmd == '1')
			{
			showcollisionregs();
			getkey();
			}

		SDL_UpdateRect(pSurface, 0, 0, outScreenWidth, outScreenHeight);

		} // wend

	if (options.audio)
		SDL_PauseAudio(0);

	debugging = 0;

	return 0;
}


// ******************************************************************************
// Win32_SDL main startup
// ******************************************************************************

int main(int argc, char *argv[])
{
	char *filename;
	char text[256];
	int loadRom = 0;
	int showMenu = 0;

	// open log file - don't care if it fails :P
	logfile = fopen("5200.log", "w");

	HostLog((char*)emu_version);
	HostLog("\n");

#ifdef _DEBUG
	sprintf(text, "argc: %d, argv[0]: %s\n", argc, argv[1]);
	HostLog(text);
#endif

	filename = NULL;
	if (argc > 1)
		filename = argv[1];

	// Initialise the emu engine
	if (jum52.Initialise() == 0)
		{
		// Initialise platform stuff ***
		if (Init() < 0)
			return 0;

		//set our at exit function
		atexit(SDL_Quit);

		jum52.controller1.mode = CONT_MODE_DIGITAL;
		jum52.controller2.mode = CONT_MODE_DIGITAL;

#ifdef _DEBUG
		sprintf(text, "vid address: %08X\n", vid);
		HostLog(text);
#endif
		// create graphics buffer, and fiddle it so that it
		// uses memory block allocated by Jum52.
		gfxdest = vid;

#ifdef _DEBUG
		sprintf(text, "Videomode: %d  snd_buf_size: %d\n", options.videomode, snd_buf_size);
		HostLog(text);
#endif
		frame_position = -9;
		exit_app = 0;

		// Whether to show the menu or load the specified file
		loadRom = (NULL == filename) ? 0 : 1;
		showMenu = (NULL == filename) ? 1 : 0;

		// main loop
		while (!exit_app)
			{
			// Select rom to load from menu
			if (showMenu)
				{
				while (1)
					{
					int ret = DoOptionsMenu();
					if (0 == ret && filename != NULL)
						{	// resume game
						break;
						}
					else if (1 == ret)
						{		// load rom
						filename = DoRomMenu();
						if (NULL == filename)
							{
							// no roms found, exit
							HostLog("No roms found!\n");
							exit(1);
							}
						else if (0 == strcmp(filename, "esc"))
							{
							// user escaped from rom menu
							filename = NULL;
							}
						else
							{
							// user selected a rom - continue
							loadRom = 1;
							}
						// else show options menu again
						}
					else if (8 == ret)
						{				// quit
						exit_app = 1;
						break;
						}
					}
				}

			if (1 == exit_app)
				break;

			clrEmuScreen(0x72);
			jprintf_y = 16;

			// Load a rom if specified
			if (loadRom && filename)
				{
				if (0 == jum52.LoadROM(filename))
					{
					sprintf(text, "Rom '%s' loaded OK.\n", filename);
					HostLog(text);
					}
				else
					{
					sprintf(text, "Could not load %s !", filename);
					HostLog(text);
					filename = NULL;
					}
				}
			loadRom = 0;
			showMenu = 1;

			// Run if loaded OK
			if (filename != NULL)
				{
				clrEmuScreen(0x00);
				//SetupPlatformVideoOutput();
				if (options.audio)
					SDL_PauseAudio(0);

				jum52.Emulate();

				if (options.audio)
					SDL_PauseAudio(1);
				//TeardownPlatformVideoOutput();
				}
			} // end while
		}
	else
		{
		HostLog("Fatal error: Could not initialise Jum52!");
		}

	if (logfile)
		fclose(logfile);

	return 0;
}

// THAT'S ALL FOLKS!
