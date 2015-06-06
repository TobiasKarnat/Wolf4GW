//
//	ID Engine
//	ID_IN.c - Input Manager
//	v1.0d1
//	By Jason Blochowiak
//

//
//	This module handles dealing with the various input devices
//
//	Depends on: Memory Mgr (for demo recording), Sound Mgr (for timing stuff),
//				User Mgr (for command line parms)
//
//	Globals:
//		LastScan - The keyboard scan code of the last key pressed
//		LastASCII - The ASCII value of the last key pressed
//	DEBUG - there are more globals
//

#include <dos.h>
#include <i86.h>

#define	KeyInt		9	// The keyboard ISR number

//
// joystick constants
//
#define	JoyScaleMax		32768
#define	JoyScaleShift	8
#define	MaxJoyValue		5000

/*
=============================================================================

					GLOBAL VARIABLES

=============================================================================
*/

//
// configuration variables
//
boolean			MousePresent;
boolean			JoysPresent[MaxJoys];
boolean			JoyPadPresent;

union REGS regs;


// 	Global variables
		boolean		Keyboard[NumCodes];
		boolean		Paused;
		char		LastASCII;
		ScanCode	LastScan;

		KeyboardDef	KbdDefs = {0x1d,0x38,0x47,0x48,0x49,0x4b,0x4d,0x4f,0x50,0x51};
		JoystickDef	JoyDefs[MaxJoys];
		ControlType	Controls[MaxPlayers];

		longword	MouseDownCount;

		Demo		DemoMode = demo_Off;
		byte 		*DemoBuffer;	// _seg
		word		DemoOffset,DemoSize;

/*
=============================================================================

					LOCAL VARIABLES

=============================================================================
*/
static	byte        ASCIINames[] =		// Unshifted ASCII for scan codes
					{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,27 ,'1','2','3','4','5','6','7','8','9','0','-','=',8  ,9  ,	// 0
	'q','w','e','r','t','y','u','i','o','p','[',']',13 ,0  ,'a','s',	// 1
	'd','f','g','h','j','k','l',';',39 ,'`',0  ,92 ,'z','x','c','v',	// 2
	'b','n','m',',','.','/',0  ,'*',0  ,' ',0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,'7','8','9','-','4','5','6','+','1',	// 4
	'2','3','0',127,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0		// 7
					};
static byte ShiftNames[] =		// Shifted ASCII for scan codes
					{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,27 ,'!','@','#','$','%','^','&','*','(',')','_','+',8  ,9  ,	// 0
	'Q','W','E','R','T','Y','U','I','O','P','{','}',13 ,0  ,'A','S',	// 1
	'D','F','G','H','J','K','L',':',34 ,'~',0  ,'|','Z','X','C','V',	// 2
	'B','N','M','<','>','?',0  ,'*',0  ,' ',0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,'7','8','9','-','4','5','6','+','1',	// 4
	'2','3','0',127,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0   	// 7
					};
static byte SpecialNames[] =	// ASCII for 0xe0 prefixed codes
					{
//	 0   1   2   3   4   5   6   7   8   9   A   B   C   D   E   F
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 0
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,13 ,0  ,0  ,0  ,	// 1
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 2
	0  ,0  ,0  ,0  ,0  ,'/',0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 3
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 4
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 5
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,	// 6
	0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0  ,0   	// 7
					};


static	boolean		IN_Started;
static	boolean		CapsLock;
static	ScanCode	CurCode,LastCode;

static	Direction	DirTable[] =		// Quick lookup for total direction
					{
						dir_NorthWest,	dir_North,	dir_NorthEast,
						dir_West,		dir_None,	dir_East,
						dir_SouthWest,	dir_South,	dir_SouthEast
					};

static	void (__interrupt *OldKeyVect)(void);

static	char			*ParmStrings_id_in[] = {"nojoys","nomouse",0};

boolean NumLockPanic=false;

byte scanbuffer[16];
int curscanoffs=0;

//	Internal routines

///////////////////////////////////////////////////////////////////////////
//
//	INL_KeyService() - Handles a keyboard interrupt (key up/down)
//
///////////////////////////////////////////////////////////////////////////
static void __interrupt
INL_KeyService(void)
{
	static	boolean	special;
	static int pausecount=5;
	byte	k,c;
	byte temp;

	k = inp(0x60);	// Get the scan code

	curscanoffs=(curscanoffs+1)&15;
	scanbuffer[curscanoffs]=k;

	// Tell the XT keyboard controller to clear the key
	outp(0x61,(temp = inp(0x61)) | 0x80);
	outp(0x61,temp);

	if(pausecount<5)		// filter 0x1d 0x45 0xe1 0x9d 0xc5 after pause key's 0xe1
		pausecount++;
	else if (k == 0xe0)		// Special key prefix
		special = true;
	else if (k == 0xe1)	// Handle Pause key
	{
		Paused = true;
		pausecount=0;
		// set fake pause code (really is scrolllock) for IN_Ack to notice a key press
		LastScan = 0x46;
	}
	else
	{
		if (k & 0x80)	// Break code
		{
			k &= 0x7f;

// DEBUG - handle special keys: ctl-alt-delete, print scrn

			if(!special || k!=sc_LShift)
				Keyboard[k] = false;
		}
		else			// Make code
		{
			if(special && k==sc_LShift)
				NumLockPanic = true;
			else
			{
				LastCode = CurCode;
				CurCode = LastScan = k;
				Keyboard[k] = true;

				if (special)
					c = SpecialNames[k];
				else
				{
					if (k == sc_CapsLock)
					{
						CapsLock ^= true;
						// DEBUG - make caps lock light work
					}
	
					if (Keyboard[sc_LShift] || Keyboard[sc_RShift])	// If shifted
					{
						c = ShiftNames[k];
						if ((c >= 'A') && (c <= 'Z') && CapsLock)
							c += 'a' - 'A';
					}
					else
					{
						c = ASCIINames[k];
						if ((c >= 'a') && (c <= 'z') && CapsLock)
							c -= 'a' - 'A';
					}
				}
				if (c)
					LastASCII = c;
			}
		}
		special = false;
	}

	outp(0x20,0x20);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetMouseDelta() - Gets the amount that the mouse has moved from the
//		mouse driver
//
///////////////////////////////////////////////////////////////////////////
static void
INL_GetMouseDelta(int *x,int *y)
{
	Mouse(MDelta);
	*x = (short)_CX;
	*y = (short)_DX;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetMouseButtons() - Gets the status of the mouse buttons from the
//		mouse driver
//
///////////////////////////////////////////////////////////////////////////
static word
INL_GetMouseButtons(void)
{
	word	buttons;

	Mouse(MButtons);
	buttons = _BX;
	return(buttons);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_GetJoyAbs() - Reads the absolute position of the specified joystick
//
///////////////////////////////////////////////////////////////////////////
void
IN_GetJoyAbs(word joy,word *xp,word *yp)
{
	byte	xb,yb,
			xs,ys;
	word	x,y;

	x = y = 0;
	xs = joy? 2 : 0;		// Find shift value for x axis
	xb = 1 << xs;			// Use shift value to get x bit mask
	ys = joy? 3 : 1;		// Do the same for y axis
	yb = 1 << ys;

// Read the absolute joystick values
	_asm {
//			pushf				// Save some registers
			push		esi
			push		edi
			cli					// Make sure an interrupt doesn't screw the timings


			mov		dx,0x201
			in			al,dx
			out		dx,al		// Clear the resistors

			mov		ah,[xb]		// Get masks into registers
			mov		ch,[yb]

			xor		si,si		// Clear count registers
			xor		di,di
			xor		bh,bh		// Clear high byte of bx for later

			push		ebp			// Don't mess up stack frame
			mov		bp,MaxJoyValue
looplabel:
			in		al,dx		// Get bits indicating whether all are finished

			dec		bp			// Check bounding register
			jz		done		// We have a silly value - abort

			mov		bl,al		// Duplicate the bits
			and		bl,ah		// Mask off useless bits (in [xb])
			add		si,bx		// Possibly increment count register
			mov		cl,bl		// Save for testing later

			mov		bl,al
			and		bl,ch		// [yb]
			add		di,bx

			add		cl,bl
			jnz		looplabel 		// If both bits were 0, drop out
			dec bp

done:
			pop		ebp

			mov		cl,[xs]		// Get the number of bits to shift
			shr		si,cl		//  and shift the count that many times

			mov		cl,[ys]
			shr		di,cl

			mov		[x],si		// Store the values into the variables
			mov		[y],di

			pop		edi
			pop		esi
//			popf				// Restore the registers
			sti
	}

	*xp = x;
	*yp = y;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetJoyDelta() - Returns the relative movement of the specified
//		joystick (from +/-127)
//
///////////////////////////////////////////////////////////////////////////
void INL_GetJoyDelta(word joy,int *dx,int *dy)
{
	word		x,y;
	JoystickDef	*def;
	static	longword	lasttime;

	IN_GetJoyAbs(joy,&x,&y);
	def = JoyDefs + joy;

	if (x < def->threshMinX)
	{
		if (x < def->joyMinX)
			x = def->joyMinX;

		x = -(x - def->threshMinX);
		x *= def->joyMultXL;
		x >>= JoyScaleShift;
		*dx = (x > 127)? -127 : -x;
	}
	else if (x > def->threshMaxX)
	{
		if (x > def->joyMaxX)
			x = def->joyMaxX;

		x = x - def->threshMaxX;
		x *= def->joyMultXH;
		x >>= JoyScaleShift;
		*dx = (x > 127)? 127 : x;
	}
	else
		*dx = 0;

	if (y < def->threshMinY)
	{
		if (y < def->joyMinY)
			y = def->joyMinY;

		y = -(y - def->threshMinY);
		y *= def->joyMultYL;
		y >>= JoyScaleShift;
		*dy = (y > 127)? -127 : -y;
	}
	else if (y > def->threshMaxY)
	{
		if (y > def->joyMaxY)
			y = def->joyMaxY;

		y = y - def->threshMaxY;
		y *= def->joyMultYH;
		y >>= JoyScaleShift;
		*dy = (y > 127)? 127 : y;
	}
	else
		*dy = 0;

	lasttime = TimeCount;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_GetJoyButtons() - Returns the button status of the specified
//		joystick
//
///////////////////////////////////////////////////////////////////////////
static word
INL_GetJoyButtons(word joy)
{
	register word	result;

	result = inp(0x201);	// Get all the joystick buttons
	result >>= joy? 6 : 4;	// Shift into bits 0-1
	result &= 3;				// Mask off the useless bits
	result ^= 3;
	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_GetJoyButtonsDB() - Returns the de-bounced button status of the
//		specified joystick
//
///////////////////////////////////////////////////////////////////////////
word
IN_GetJoyButtonsDB(word joy)
{
	longword	lasttime;
	word		result1,result2;

	do
	{
		result1 = INL_GetJoyButtons(joy);
		lasttime = TimeCount;
		while (TimeCount == lasttime)
			;
		result2 = INL_GetJoyButtons(joy);
	} while (result1 != result2);
	return(result1);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_StartKbd() - Sets up my keyboard stuff for use
//
///////////////////////////////////////////////////////////////////////////
static void
INL_StartKbd(void)
{
	IN_ClearKeysDown();

	OldKeyVect = _dos_getvect(KeyInt);
	_dos_setvect(KeyInt,INL_KeyService);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_ShutKbd() - Restores keyboard control to the BIOS
//
///////////////////////////////////////////////////////////////////////////
static void
INL_ShutKbd(void)
{
	*(word *)0x0417=(*(word *)0x0417) & 0xfaf0;	// Clear ctrl/alt/shift flags

	_dos_setvect(KeyInt,OldKeyVect);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_StartMouse() - Detects and sets up the mouse
//
///////////////////////////////////////////////////////////////////////////
static boolean
INL_StartMouse(void)
{
#if 0
	if (getvect(MouseInt))
	{
		Mouse(MReset);
		if (_AX == 0xffff)
			return(true);
	}
	return(false);
#endif
	unsigned char *vector;

	if ((vector=(unsigned char *)(((long)*((word *)(0x33*4+2)))*16+(long)*((word *)(0x33*4))))==NULL)
   	return false;

	if (*vector == 207)
   	return false;

	Mouse(MReset);
	return true;
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_ShutMouse() - Cleans up after the mouse
//
///////////////////////////////////////////////////////////////////////////
static void
INL_ShutMouse(void)
{
}

//
//	INL_SetJoyScale() - Sets up scaling values for the specified joystick
//
static void
INL_SetJoyScale(word joy)
{
	JoystickDef	*def;

	def = &JoyDefs[joy];
	def->joyMultXL = JoyScaleMax / (def->threshMinX - def->joyMinX);
	def->joyMultXH = JoyScaleMax / (def->joyMaxX - def->threshMaxX);
	def->joyMultYL = JoyScaleMax / (def->threshMinY - def->joyMinY);
	def->joyMultYH = JoyScaleMax / (def->joyMaxY - def->threshMaxY);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_SetupJoy() - Sets up thresholding values and calls INL_SetJoyScale()
//		to set up scaling values
//
///////////////////////////////////////////////////////////////////////////
void
IN_SetupJoy(word joy,word minx,word maxx,word miny,word maxy)
{
	word		d,r;
	JoystickDef	*def;

	def = &JoyDefs[joy];

	def->joyMinX = minx;
	def->joyMaxX = maxx;
	r = maxx - minx;
	d = r / 3;
	def->threshMinX = ((r / 2) - d) + minx;
	def->threshMaxX = ((r / 2) + d) + minx;

	def->joyMinY = miny;
	def->joyMaxY = maxy;
	r = maxy - miny;
	d = r / 3;
	def->threshMinY = ((r / 2) - d) + miny;
	def->threshMaxY = ((r / 2) + d) + miny;

	INL_SetJoyScale(joy);
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_StartJoy() - Detects & auto-configures the specified joystick
//					The auto-config assumes the joystick is centered
//
///////////////////////////////////////////////////////////////////////////
static boolean
INL_StartJoy(word joy)
{
	word		x,y;

	IN_GetJoyAbs(joy,&x,&y);

	if
	(
		((x == 0) || (x > MaxJoyValue - 10))
	||	((y == 0) || (y > MaxJoyValue - 10))
	)
		return(false);
	else
	{
		IN_SetupJoy(joy,0,x * 2,0,y * 2);
		return(true);
	}
}

///////////////////////////////////////////////////////////////////////////
//
//	INL_ShutJoy() - Cleans up the joystick stuff
//
///////////////////////////////////////////////////////////////////////////
static void
INL_ShutJoy(word joy)
{
	JoysPresent[joy] = false;
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_Startup() - Starts up the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void
IN_Startup(void)
{
	boolean	checkjoys,checkmouse;
	word	i;

	if (IN_Started)
		return;

	checkjoys = true;
	checkmouse = true;
	for (i = 1;i < __argc;i++)
	{
		switch (US_CheckParm(__argv[i],ParmStrings_id_in))
		{
		case 0:
			checkjoys = false;
			break;
		case 1:
			checkmouse = false;
			break;
		}
	}

	INL_StartKbd();
	MousePresent = checkmouse? INL_StartMouse() : false;

	for (i = 0;i < MaxJoys;i++)
		JoysPresent[i] = checkjoys? INL_StartJoy(i) : false;

	IN_Started = true;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Default() - Sets up default conditions for the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void
IN_Default(boolean gotit,ControlType in)
{
	if
	(
		(!gotit)
	|| 	((in == ctrl_Joystick1) && !JoysPresent[0])
	|| 	((in == ctrl_Joystick2) && !JoysPresent[1])
	|| 	((in == ctrl_Mouse) && !MousePresent)
	)
		in = ctrl_Keyboard1;
	IN_SetControlType(0,in);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Shutdown() - Shuts down the Input Mgr
//
///////////////////////////////////////////////////////////////////////////
void
IN_Shutdown(void)
{
	word	i;

	if (!IN_Started)
		return;

	INL_ShutMouse();
	for (i = 0;i < MaxJoys;i++)
		INL_ShutJoy(i);
	INL_ShutKbd();

	IN_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_ClearKeysDown() - Clears the keyboard array
//
///////////////////////////////////////////////////////////////////////////
void
IN_ClearKeysDown(void)
{
	LastScan = sc_None;
	LastASCII = key_None;
	memset (Keyboard,0,sizeof(Keyboard));
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_ReadControl() - Reads the device associated with the specified
//		player and fills in the control info struct
//
///////////////////////////////////////////////////////////////////////////
void
IN_ReadControl(int player,ControlInfo *info)
{
	boolean		realdelta;
	byte		dbyte;
	word		buttons;
	int			dx,dy;
	Motion		mx,my;
	ControlType	type;
	register KeyboardDef	*def;

	dx = dy = 0;
	mx = my = motion_None;
	buttons = 0;

	if (DemoMode == demo_Playback)
	{
		dbyte = DemoBuffer[DemoOffset + 1];
		my = (Motion) ((dbyte & 3) - 1);
		mx = (Motion) (((dbyte >> 2) & 3) - 1);
		buttons = (dbyte >> 4) & 3;

		if (!(--DemoBuffer[DemoOffset]))
		{
			DemoOffset += 2;
			if (DemoOffset >= DemoSize)
				DemoMode = demo_PlayDone;
		}

		realdelta = false;
	}
	else if (DemoMode == demo_PlayDone)
		Quit("Demo playback exceeded");
	else
	{
		switch (type = Controls[player])
		{
		case ctrl_Keyboard:
			def = &KbdDefs;

			if (Keyboard[def->upleft])
				mx = motion_Left,my = motion_Up;
			else if (Keyboard[def->upright])
				mx = motion_Right,my = motion_Up;
			else if (Keyboard[def->downleft])
				mx = motion_Left,my = motion_Down;
			else if (Keyboard[def->downright])
				mx = motion_Right,my = motion_Down;

			if (Keyboard[def->up])
				my = motion_Up;
			else if (Keyboard[def->down])
				my = motion_Down;

			if (Keyboard[def->left])
				mx = motion_Left;
			else if (Keyboard[def->right])
				mx = motion_Right;

			if (Keyboard[def->button0])
				buttons += 1 << 0;
			if (Keyboard[def->button1])
				buttons += 1 << 1;
			realdelta = false;
			break;
		case ctrl_Joystick1:
		case ctrl_Joystick2:
			INL_GetJoyDelta(type - ctrl_Joystick,&dx,&dy);
			buttons = INL_GetJoyButtons(type - ctrl_Joystick);
			realdelta = true;
			break;
		case ctrl_Mouse:
			INL_GetMouseDelta(&dx,&dy);
			buttons = INL_GetMouseButtons();
			realdelta = true;
			break;
		}
	}

	if (realdelta)
	{
		mx = (dx < 0)? motion_Left : ((dx > 0)? motion_Right : motion_None);
		my = (dy < 0)? motion_Up : ((dy > 0)? motion_Down : motion_None);
	}
	else
	{
		dx = mx * 127;
		dy = my * 127;
	}

	info->x = dx;
	info->xaxis = mx;
	info->y = dy;
	info->yaxis = my;
	info->button0 = buttons & (1 << 0);
	info->button1 = buttons & (1 << 1);
	info->button2 = buttons & (1 << 2);
	info->button3 = buttons & (1 << 3);
	info->dir = DirTable[((my + 1) * 3) + (mx + 1)];

	if (DemoMode == demo_Record)
	{
		// Pack the control info into a byte
		dbyte = (buttons << 4) | ((mx + 1) << 2) | (my + 1);

		if	((DemoBuffer[DemoOffset + 1] == dbyte)	&&	(DemoBuffer[DemoOffset] < 255))
			(DemoBuffer[DemoOffset])++;
		else
		{
			if (DemoOffset || DemoBuffer[DemoOffset])
				DemoOffset += 2;

			if (DemoOffset >= DemoSize)
				Quit("Demo buffer overflow");

			DemoBuffer[DemoOffset] = 1;
			DemoBuffer[DemoOffset + 1] = dbyte;
		}
	}
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_SetControlType() - Sets the control type to be used by the specified
//		player
//
///////////////////////////////////////////////////////////////////////////
void
IN_SetControlType(int player,ControlType type)
{
	// DEBUG - check that requested type is present?
	Controls[player] = type;
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_WaitForKey() - Waits for a scan code, then clears LastScan and
//		returns the scan code
//
///////////////////////////////////////////////////////////////////////////
ScanCode
IN_WaitForKey(void)
{
	ScanCode	result;

	while ((result = LastScan)==0)
		;
	LastScan = 0;
	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_WaitForASCII() - Waits for an ASCII char, then clears LastASCII and
//		returns the ASCII value
//
///////////////////////////////////////////////////////////////////////////
char
IN_WaitForASCII(void)
{
	char		result;

	while ((result = LastASCII)==0)
		;
	LastASCII = '\0';
	return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//	IN_Ack() - waits for a button or key press.  If a button is down, upon
// calling, it must be released for it to be recognized
//
///////////////////////////////////////////////////////////////////////////

boolean	btnstate[8];

void IN_StartAck(void)
{
	unsigned i,buttons;

//
// get initial state of everything
//
	IN_ClearKeysDown();
	memset (btnstate,0,sizeof(btnstate));

	buttons = IN_JoyButtons () << 4;
	if (MousePresent)
		buttons |= IN_MouseButtons ();

	for (i=0;i<8;i++,buttons>>=1)
		if (buttons&1)
			btnstate[i] = true;
}


boolean IN_CheckAck (void)
{
	unsigned i,buttons;

//
// see if something has been pressed
//
	if (LastScan)
		return true;

	buttons = IN_JoyButtons () << 4;
	if (MousePresent)
		buttons |= IN_MouseButtons ();

	for (i=0;i<8;i++,buttons>>=1)
		if ( buttons&1 )
		{
			if (!btnstate[i])
				return true;
		}
		else
			btnstate[i]=false;

	return false;
}


void IN_Ack (void)
{
	if(!IN_Started) { getch(); return; }
	
	
	IN_StartAck ();

	while (!IN_CheckAck ())
	;
}


///////////////////////////////////////////////////////////////////////////
//
//	IN_UserInput() - Waits for the specified delay time (in ticks) or the
//		user pressing a key or a mouse button. If the clear flag is set, it
//		then either clears the key or waits for the user to let the mouse
//		button up.
//
///////////////////////////////////////////////////////////////////////////
boolean IN_UserInput(longword delay)
{
	longword	lasttime;

	lasttime = TimeCount;
	IN_StartAck ();
	do
	{
		if (IN_CheckAck())
			return true;
	} while (TimeCount - lasttime < delay);
	return(false);
}

//===========================================================================

/*
===================
=
= IN_MouseButtons
=
===================
*/

byte	IN_MouseButtons (void)
{
	if (MousePresent)
	{
		Mouse(MButtons);
		return (byte)_BX;
	}
	else
		return 0;
}


/*
===================
=
= IN_JoyButtons
=
===================
*/

byte	IN_JoyButtons (void)
{
	unsigned joybits;

	joybits = inp(0x201);	// Get all the joystick buttons
	joybits >>= 4;				// only the high bits are useful
	joybits ^= 15;				// return with 1=pressed

	return (byte)joybits;
}


