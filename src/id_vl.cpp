// ID_VL.C

#include <dos.h>
#include <mem.h>
#include <string.h>
#include <conio.h>
#pragma hdrstop

//
// SC_INDEX is expected to stay at SC_MAPMASK for proper operation
//

byte *vbuf=(byte *)0xa0000;
byte *vdisp=(byte *)0xa0000;

unsigned linewidth=80;

boolean		screenfaded;
unsigned bordercolor;

byte		palette1[256][3], palette2[256][3];

//===========================================================================


/*
=======================
=
= VL_Shutdown
=
=======================
*/

void	VL_Shutdown (void)
{
	VL_SetTextMode ();
}


/*
=======================
=
= VL_SetVGAPlaneMode
=
=======================
*/

void	VL_SetVGAPlaneMode (void)
{
	_asm {
		mov eax,0013h
		int 10h
		
		mov edx,03c4h
		mov al,4
		out dx,al
		inc edx
		in al,dx
		and al,0f7h
		or al,4
		out dx,al
		
		mov	edx,03ceh
		mov al,5
		out dx,al
		inc edx
		in al,dx
		and al,0ech
		out dx,al
		
		dec edx
		mov al,6
		out dx,al
		inc edx
		in al,dx
		and al,0fdh
		out dx,al
		
		mov edx,03d4h
		mov al,20
		out dx,al
		inc edx
		in al,dx
		and al,0b0h
		out dx,al
		
		dec edx
		mov al,23
		out dx,al
		inc edx
		in al,dx
		or al,040h
		out dx,al
   }
	VGAMAPMASK(15);
}

/*
=======================
=
= VL_SetTextMode
=
=======================
*/

void	VL_SetTextMode (void)
{
	_asm {
		mov	eax,3
		int	0x10
	}
}

/*
=============================================================================

						PALETTE OPS

		To avoid snow, do a WaitVBL BEFORE calling these

=============================================================================
*/


/*
=================
=
= VL_FillPalette
=
=================
*/

void VL_FillPalette (int red, int green, int blue)
{
	int	i;

	outportb (PEL_WRITE_ADR,0);
	for (i=0;i<256;i++)
	{
		outportb (PEL_DATA,red);
		outportb (PEL_DATA,green);
		outportb (PEL_DATA,blue);
	}
}

//===========================================================================

/*
=================
=
= VL_SetColor
=
=================
*/

void VL_SetColor	(int color, int red, int green, int blue)
{
	outportb (PEL_WRITE_ADR,color);
	outportb (PEL_DATA,red);
	outportb (PEL_DATA,green);
	outportb (PEL_DATA,blue);
}

//===========================================================================

/*
=================
=
= VL_GetColor
=
=================
*/

void VL_GetColor	(int color, int *red, int *green, int *blue)
{
	outportb (PEL_READ_ADR,color);
	*red = inportb (PEL_DATA);
	*green = inportb (PEL_DATA);
	*blue = inportb (PEL_DATA);
}

//===========================================================================

/*
=================
=
= VL_SetPalette
=
= If fast palette setting has been tested for, it is used
= (some cards don't like outsb palette setting)
=
=================
*/

void VL_SetPalette (byte *palette)
{
/*	int	i;

	outportb (PEL_WRITE_ADR,0);
	for (i=0;i<768;i++)
		outportb(PEL_DATA,*palette++);*/

	_asm {
		mov	edx,PEL_WRITE_ADR
		xor	al,al
		out	dx,al
		mov	edx,PEL_DATA
		mov	esi,dword ptr palette
		mov	ecx,768
		rep	outsb
	}
}


//===========================================================================

/*
=================
=
= VL_GetPalette
=
= This does not use the port string instructions,
= due to some incompatabilities
=
=================
*/

void VL_GetPalette (byte *palette)
{
	int	i;

	outportb (PEL_READ_ADR,0);
	for (i=0;i<768;i++)
		*palette++ = inportb(PEL_DATA);
}


//===========================================================================

/*
=================
=
= VL_FadeOut
=
= Fades the current palette to the given color in the given number of steps
=
=================
*/

void VL_FadeOut (int start, int end, int red, int green, int blue, int steps)
{
	int		i,j,orig,delta;
	byte	*origptr, *newptr;

	VL_WaitVBL(1);
	VL_GetPalette (&palette1[0][0]);
	memcpy (palette2,palette1,768);

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		origptr = &palette1[start][0];
		newptr = &palette2[start][0];
		for (j=start;j<=end;j++)
		{
			orig = *origptr++;
			delta = red-orig;
			*newptr++ = orig + delta * i / steps;
			orig = *origptr++;
			delta = green-orig;
			*newptr++ = orig + delta * i / steps;
			orig = *origptr++;
			delta = blue-orig;
			*newptr++ = orig + delta * i / steps;
		}

		VL_WaitVBL(1);
		VL_SetPalette (&palette2[0][0]);
	}

//
// final color
//
	VL_FillPalette (red,green,blue);

	screenfaded = true;
}


/*
=================
=
= VL_FadeIn
=
=================
*/

void VL_FadeIn (int start, int end, byte *palette, int steps)
{
	int		i,j,delta;

	VL_WaitVBL(1);
	VL_GetPalette (&palette1[0][0]);
	memcpy (&palette2[0][0],&palette1[0][0],sizeof(palette1));

	start *= 3;
	end = end*3+2;

//
// fade through intermediate frames
//
	for (i=0;i<steps;i++)
	{
		for (j=start;j<=end;j++)
		{
			delta = palette[j]-palette1[0][j];
			palette2[0][j] = palette1[0][j] + delta * i / steps;
		}

		VL_WaitVBL(1);
		VL_SetPalette (&palette2[0][0]);
	}

//
// final color
//
	VL_SetPalette (palette);
	screenfaded = false;
}

/*
=============================================================================

							PIXEL OPS

=============================================================================
*/

byte	pixmasks[4] = {1,2,4,8};
byte	leftmasks[4] = {15,14,12,8};
byte	rightmasks[4] = {1,3,7,15};

/*
=================
=
= VL_Plot
=
=================
*/

void VL_Plot (int x, int y, int color)
{
	byte mask;

	mask = pixmasks[x&3];
	VGAMAPMASK(mask);
	vbuf[y*80+(x>>2)]=color;
	VGAMAPMASK(15);
}


/*
=================
=
= VL_Hlin
=
=================
*/

void VL_Hlin (unsigned x, unsigned y, unsigned width, int color)
{
	unsigned		xbyte;
	byte			*dest;
	byte			leftmask,rightmask;
	int				midbytes;

	xbyte = x>>2;
	leftmask = leftmasks[x&3];
	rightmask = rightmasks[(x+width-1)&3];
	midbytes = ((x+width+3)>>2) - xbyte - 2;

	dest = vbuf+y*80+xbyte;

	if (midbytes<0)
	{
	// all in one byte
		VGAMAPMASK(leftmask&rightmask);
		*dest = color;
		VGAMAPMASK(15);
		return;
	}

	VGAMAPMASK(leftmask);
	*dest++ = color;

	VGAMAPMASK(15);
	memset (dest,color,midbytes);
	dest+=midbytes;

	VGAMAPMASK(rightmask);
	*dest = color;

	VGAMAPMASK(15);
}


/*
=================
=
= VL_Vlin
=
=================
*/

void VL_Vlin (int x, int y, int height, int color)
{
	byte *dest;
	byte mask;

	mask = pixmasks[x&3];
	VGAMAPMASK(mask);

	dest = vbuf+y*80+(x>>2);

	while (height--)
	{
		*dest = color;
		dest += linewidth;
	}

	VGAMAPMASK(15);
}


/*
=================
=
= VL_Bar
=
=================
*/

void VL_Bar (int x, int y, int width, int height, int color)
{
	byte	*dest;
	byte	leftmask,rightmask;
	int		midbytes,linedelta;

	leftmask = leftmasks[x&3];
	rightmask = rightmasks[(x+width-1)&3];
	midbytes = ((x+width+3)>>2) - (x>>2) - 2;
	linedelta = linewidth-(midbytes+1);

	dest = vbuf+y*80+(x>>2);

	if (midbytes<0)
	{
	// all in one byte
		VGAMAPMASK(leftmask&rightmask);
		while (height--)
		{
			*dest = color;
			dest += linewidth;
		}
		VGAMAPMASK(15);
		return;
	}

	while (height--)
	{
		VGAMAPMASK(leftmask);
		*dest++ = color;

		VGAMAPMASK(15);
		memset (dest,color,midbytes);
		dest+=midbytes;

		VGAMAPMASK(rightmask);
		*dest = color;

		dest+=linedelta;
	}

	VGAMAPMASK(15);
}

/*
============================================================================

							MEMORY OPS

============================================================================
*/

/*
=================
=
= VL_MemToLatch
=
=================
*/

void VL_MemToLatch (byte *source, int width, int height, unsigned dest)
{
	unsigned count;
	byte	plane,mask;

	count = ((width+3)/4)*height;
	mask = 1;
	for (plane = 0; plane<4 ; plane++)
	{
		VGAMAPMASK(mask);
		mask <<= 1;

		memcpy((byte *)(0xa0000+dest),source,count);

		source+= count;
	}
}


//===========================================================================


/*
=================
=
= VL_MemToScreen
=
= Draws a block of data to the screen.
=
=================
*/

void VL_MemToScreen (byte *source, int width, int height, int x, int y)
{
	byte    *screen,*dest;
	byte mask;
	int		plane;

	width>>=2;
	dest = vbuf+y*80+(x>>2);
	mask = 1 << (x&3);

	for (plane = 0; plane<4; plane++)
	{
		VGAMAPMASK(mask);
		mask <<= 1;
		if (mask == 16)
			mask = 1;

		screen = dest;
		for (y=0;y<height;y++,screen+=linewidth,source+=width)
			memcpy (screen,source,width);
	}
}

//==========================================================================

/*
=================
=
= VL_LatchToScreen
=
=================
*/

void VL_LatchToScreen (unsigned source, int width, int height, int x, int y)
{
	VGAWRITEMODE(1);
	VGAMAPMASK(15);

	byte *dest=vbuf+y*80+(x>>2);
	byte *src=(byte *)(0xa0000+source);

	_asm {
		mov	edi,dest
		mov	esi,src
		mov	ebx,linewidth
		mov	eax,width
		sub	ebx,eax
		mov	edx,height

drawline:
		mov	ecx,eax
		rep	movsb
		add	edi,ebx
		dec	edx
		jnz	drawline
	}

	VGAWRITEMODE(0);
}

//===========================================================================

/*
=================
=
= VL_ScreenToScreen
=
=================
*/

void VL_ScreenToScreen (byte *source, byte *dest,int width, int height)
{
	VGAWRITEMODE(1);
	VGAMAPMASK(15);

	_asm {
		mov	esi,[source]
		mov	edi,[dest]
		mov	eax,[width]
		mov	ebx,[linewidth]
		sub	ebx,eax
		mov	edx,[height]

drawline:
		mov	ecx,eax
		rep movsb
		add	esi,ebx
		add	edi,ebx
		dec	edx
		jnz	drawline
	}

	VGAWRITEMODE(0);
}
