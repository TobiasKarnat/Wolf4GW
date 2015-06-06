//
//      ID Engine
//      ID_SD.c - Sound Manager for Wolfenstein 3D
//      v1.2
//      By Jason Blochowiak
//

//
//      This module handles dealing with generating sound on the appropriate
//              hardware
//
//      Depends on: User Mgr (for parm checking)
//
//      Globals:
//              For User Mgr:
//                      SoundSourcePresent - Sound Source thingie present?
//                      SoundBlasterPresent - SoundBlaster card present?
//                      AdLibPresent - AdLib card present?
//                      SoundMode - What device is used for sound effects
//                              (Use SM_SetSoundMode() to set)
//                      MusicMode - What device is used for music
//                              (Use SM_SetMusicMode() to set)
//                      DigiMode - What device is used for digitized sound effects
//                              (Use SM_SetDigiDevice() to set)
//
//              For Cache Mgr:
//                      NeedsDigitized - load digitized sounds?
//                      NeedsMusic - load music?
//

#include <dos.h>

#include "wl_def.h"

#pragma hdrstop

#ifdef  nil
#undef  nil
#endif
#define nil     0

//#define BUFFERDMA
#define NOSEGMENTATION
//#define SHOWSDDEBUG

#define SDL_SoundFinished()     {SoundNumber = (soundnames) 0; SoundPriority = 0;}

// Macros for SoundBlaster stuff
#define sbOut(n,b)      outp((n) + sbLocation,b)
#define sbIn(n)         inp((n) + sbLocation)
#define sbWriteDelay()  while (sbIn(sbWriteStat) & 0x80);
#define sbReadDelay()   while (sbIn(sbDataAvail) & 0x80);

// Macros for AdLib stuff
#define selreg(n)       outp(alFMAddr,n)
#define writereg(n)     outp(alFMData,n)
#define readstat()      inp(alFMStatus)

static void alOutInIRQ(byte n,byte b);

//      Global variables
        boolean         SoundSourcePresent,
                                AdLibPresent,
                                SoundBlasterPresent,SBProPresent,
                                NeedsDigitized,NeedsMusic,
                                SoundPositioned;
        SDMode          SoundMode;
        SMMode          MusicMode;
        SDSMode         DigiMode;
        volatile longword TimeCount;
        byte **SoundTable;
        boolean         ssIsTandy;
        word            ssPort = 2;
        int                     DigiMap[LASTSOUND];

//      Internal variables
static  boolean                 SD_Started;
                boolean                 nextsoundpos;
                longword                TimerDivisor,TimerCount;
static  char                    *ParmStrings[] =
                                                {
                                                        "noal",
                                                        "nosb",
                                                        "nopro",
                                                        "noss",
                                                        "sst",
                                                        "ss1",
                                                        "ss2",
                                                        "ss3",
                                                        nil
                                                };
//static  void                    (*SoundUserHook)(void);
                soundnames              SoundNumber,DigiNumber;
                word                    SoundPriority,DigiPriority;
                int                             LeftPosition,RightPosition;
                void (__interrupt *t0OldService)(void);
                long                    LocalTime;
                word                    TimerRate;

                word                    NumDigi,DigiLeft,DigiPage;
                word                    *DigiList;
                word                    DigiLastStart,DigiLastEnd;
                boolean                 DigiPlaying;
static  boolean                 DigiMissed,DigiLastSegment;
static  memptr                  DigiNextAddr;
static  word                    DigiNextLen;

//      SoundBlaster variables
static  boolean                                 sbNoCheck,sbNoProCheck;
static  volatile boolean                sbSamplePlaying;
static  byte                                    sbOldIntMask = -1;
static  volatile byte                   *sbNextSegPtr;
static  byte                                    sbDMA = 1,
                                                                sbDMAa1 = 0x83,sbDMAa2 = 2,sbDMAa3 = 3;
static  byte                                    sba1Vals[] = {0x87,0x83,0,0x82};
static  byte                                    sba2Vals[] = {0,2,0,6};
static  byte                                    sba3Vals[] = {1,3,0,7};
static  int                                             sbLocation = -1,sbInterrupt = 7,sbIntVec = 0xf;
static  int                                             sbIntVectors[] = {-1,-1,0xa,0xb,-1,0xd,-1,0xf,-1,-1,-1};
static  volatile longword               sbNextSegLen;
static  void (__interrupt *sbOldIntHand)(void);
static  byte                                    sbpOldFMMix,sbpOldVOCMix;

//      SoundSource variables
                boolean                         ssNoCheck;
                boolean                         ssActive;
                word                            ssControl,ssStatus,ssData;
                byte                            ssOn,ssOff;
                volatile byte           *ssSample;
                volatile longword       ssLengthLeft;

//      PC Sound variables
                volatile byte   pcLastSample;
                volatile byte *pcSound;
                longword                pcLengthLeft;

//      AdLib variables
                boolean                 alNoCheck;
                byte                    *alSound;
                byte                    alBlock;
                longword                alLengthLeft;
                longword                alTimeCount;
                Instrument      alZeroInst;
                boolean         alNoIRQ;

// This table maps channel numbers to carrier and modulator op cells
static  byte                    carriers[9] =  { 3, 4, 5,11,12,13,19,20,21},
                                                modifiers[9] = { 0, 1, 2, 8, 9,10,16,17,18};

//      Sequencer variables
                boolean                 sqActive;
static  byte                    alFXReg;
                word                    *sqHack,*sqHackPtr;
                word                    sqHackLen,sqHackSeqLen;
                long                    sqHackTime;

//      Internal routines
                void                    SDL_DigitizedDoneInIRQ();

#define DMABUFFERSIZE 4096

int DMABufferDescriptor=0;
int DMABufferIndex=0;
byte *DMABuffer;

int count_time=0;
int count_fx=0;
int extreme=0;
volatile boolean pcindicate;

//volatile boolean deactivateSoundHandler=false;

boolean isSBSamplePlaying() { return sbSamplePlaying; }
byte *getSBNextSegPtr() { return (byte *) sbNextSegPtr; }

int lastsoundstarted=-1;
int lastdigiwhich=-1;
int lastdigistart=-1;
int lastdigisegstart=-1;

int DPMI_GetDOSMemory(void **ptr, int *descriptor, unsigned length);
#pragma aux DPMI_GetDOSMemory = \
        "mov    eax,0100h" \
        "add    ebx,15" \
        "shr    ebx,4" \
        "int    31h" \
        "jc     DPMI_Exit" \
        "movzx  eax,ax" \
        "shl    eax,4" \
        "mov    [esi],eax" \
        "mov    [edi],edx" \
        "sub    eax,eax" \
        "DPMI_Exit:" \
        parm [esi] [edi] [ebx] \
        modify exact [eax ebx edx]

int DPMI_FreeDOSMemory(int descriptor);
#pragma aux DPMI_FreeDOSMemory = \
        "mov    eax,0101h" \
        "int    31h" \
        "jc     DPMI_Exit" \
        "sub    eax,eax" \
        "DPMI_Exit:" \
        parm [edx] \
        modify exact [eax]

void SDL_turnOnPCSpeaker(word timerval);
#pragma aux SDL_turnOnPCSpeaker = \
        "mov    al,0b6h" \              
        "out    43h,al" \               
        "mov    al,bl" \
        "out    42h,al" \               
        "mov    al,bh" \
        "out    42h,al" \               
        "in     al,61h" \               
        "or     al,3"   \
        "out    61h,al" \
        parm [bx] \
        modify exact [al]

void SDL_turnOffPCSpeaker();
#pragma aux SDL_turnOffPCSpeaker = \
        "in     al,61h" \               
        "and    al,0fch" \
        "out    61h,al" \
        modify exact [al]

void SDL_setPCSpeaker(byte val);
#pragma aux SDL_setPCSpeaker = \
        "in     al,61h" \
        "and    al,0fch" \
        "or     al,ah" \
        "out    61h,al" \
        parm [ah] \
        modify exact [al]

void inline SDL_DoFX()
{
        if(pcSound)
        {
                if(*pcSound!=pcLastSample)
                {
                        pcLastSample=*pcSound;

                        if(pcLastSample)
                                SDL_turnOnPCSpeaker(pcLastSample*60);
                        else
                                SDL_turnOffPCSpeaker();
                }
                pcSound++;
                pcLengthLeft--;
                if(!pcLengthLeft)
                {
                        pcSound=0;
                        SoundNumber=(soundnames)0;
                        SoundPriority=0;
                        SDL_turnOffPCSpeaker();
                }                                       
        }

        if(alSound && !alNoIRQ)
        {
                if(*alSound)
                {
                        alOutInIRQ(alFreqL,*alSound);
                        alOutInIRQ(alFreqH,alBlock);
                }
                else alOutInIRQ(alFreqH,0);
                alSound++;
                alLengthLeft--;
                if(!alLengthLeft)
                {
                        alSound=0;
                        SoundNumber=(soundnames)0;
                        SoundPriority=0;
                        alOutInIRQ(alFreqH,0);
                }
        }
}

void inline SDL_DoFast()
{
        count_fx++;
        if(count_fx>=5)
        {
                count_fx=0;

                SDL_DoFX();
                
                count_time++;
                if(count_time>=2)
                {
                        TimeCount++;
                        count_time=0;
                }
        }

        if(sqActive && !alNoIRQ)
        {
                if(sqHackLen)
                {
                        do
                        {
                                if(sqHackTime>alTimeCount) break;
                                sqHackTime=alTimeCount+*(sqHackPtr+1);
                                alOutInIRQ(*(byte *)sqHackPtr,*(((byte *)sqHackPtr)+1));
                                sqHackPtr+=2;
                                sqHackLen-=4;
                        }
                        while(sqHackLen);
                }
                alTimeCount++;
                if(!sqHackLen)
                {
                        sqHackPtr=sqHack;
                        sqHackLen=sqHackSeqLen;
                        alTimeCount=0;
                        sqHackTime=0;
                }
        }

        if(ssSample)
        {
                if(!(inp(ssStatus)&0x40))
                {
                        outp(ssData,*ssSample++);
                        outp(ssControl,ssOff);
                        _asm push eax
                        _asm pop eax
                        outp(ssControl,ssOn);
                        _asm push eax
                        _asm pop eax
                        ssLengthLeft--;
                        if(!ssLengthLeft)
                        {
                                ssSample=0;
                                SDL_DigitizedDoneInIRQ();
                        }
                }
        }

        TimerCount+=TimerDivisor;
        if(*((word *)&TimerCount+1))
        {
                *((word *)&TimerCount+1)=0;
                t0OldService();
        }
        else
        {
                outp(0x20,0x20);
        }
}

// Timer 0 ISR for 7000Hz interrupts
void __interrupt SDL_t0ExtremeAsmService(void)
{
        if(pcindicate)
        {
                if(pcSound)
                {
                        SDL_setPCSpeaker(((*pcSound++)&0x80)>>6);
                        pcLengthLeft--;
                        if(!pcLengthLeft)
                        {
                                pcSound=0;
                                SDL_turnOffPCSpeaker();
                                SDL_DigitizedDoneInIRQ();
                        }
                }
        }
        extreme++;
        if(extreme>=10)
        {
                extreme=0;
                SDL_DoFast();
        }
        else
                outp(0x20,0x20);
}

// Timer 0 ISR for 700Hz interrupts
void __interrupt SDL_t0FastAsmService(void)
{
        SDL_DoFast();
}

// Timer 0 ISR for 140Hz interrupts
void __interrupt SDL_t0SlowAsmService(void)
{
        count_time++;
        if(count_time>=2)
        {
                TimeCount++;
                count_time=0;
        }

        SDL_DoFX();

        TimerCount+=TimerDivisor;
        if(*((word *)&TimerCount+1))
        {
                *((word *)&TimerCount+1)=0;
                t0OldService();
        }
        else
                outp(0x20,0x20);
}

void SDL_IndicatePC(boolean ind)
{
        pcindicate=ind;
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SetTimer0() - Sets system timer 0 to the specified speed
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_SetTimer0(word speed)
{
#ifndef TPROF   // If using Borland's profiling, don't screw with the timer
//      _asm pushfd
        _asm cli

        outp(0x43,0x36);                                // Change timer 0
        outp(0x40,(byte)speed);
        outp(0x40,speed >> 8);
        // Kludge to handle special case for digitized PC sounds
        if (TimerDivisor == (1192030 / (TickBase * 100)))
                TimerDivisor = (1192030 / (TickBase * 10));
        else
                TimerDivisor = speed;

//      _asm popfd
        _asm    sti
#else
        TimerDivisor = 0x10000;
#endif
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SetIntsPerSec() - Uses SDL_SetTimer0() to set the number of
//              interrupts generated by system timer 0 per second
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_SetIntsPerSec(word ints)
{
        TimerRate = ints;
        SDL_SetTimer0(1192030 / ints);
}

static void
SDL_SetTimerSpeed(void)
{
        word    rate;
        void (_interrupt *isr)(void);

        if ((DigiMode == sds_PC) && DigiPlaying)
        {
                rate = TickBase * 100;
                isr = SDL_t0ExtremeAsmService;
        }
        else if ((MusicMode == smm_AdLib) || ((DigiMode == sds_SoundSource) && DigiPlaying)     )
        {
                rate = TickBase * 10;
                isr = SDL_t0FastAsmService;
        }
        else
        {
                rate = TickBase * 2;
                isr = SDL_t0SlowAsmService;
        }

        if (rate != TimerRate)
        {
                _dos_setvect(8,isr);
                SDL_SetIntsPerSec(rate);
                TimerRate = rate;
        }
}

//
//      SoundBlaster code
//

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SBStopSample() - Stops any active sampled sound and causes DMA
//              requests from the SoundBlaster to cease
//
///////////////////////////////////////////////////////////////////////////
static void SDL_SBStopSampleInIRQ(void)
{
        byte    is;

        if (sbSamplePlaying)
        {
                sbSamplePlaying = false;

                sbWriteDelay();
//              sbOut(sbWriteCmd,0xd0); // Turn off DSP DMA
                sbOut(sbWriteCmd,0xda); // exit autoinitialise (stop dma transfer in vdmsound)

                is = inp(0x21); // Restore interrupt mask bit
                if (sbOldIntMask & (1 << sbInterrupt))
                        is |= (1 << sbInterrupt);
                else
                        is &= ~(1 << sbInterrupt);
                outp(0x21,is);
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SBPlaySeg() - Plays a chunk of sampled sound on the SoundBlaster
//      Insures that the chunk doesn't cross a bank boundary, programs the DMA
//       controller, and tells the SB to start doing DMA requests for DAC
//
///////////////////////////////////////////////////////////////////////////

// data must not overlap a 64k boundary and must be in main memory

static longword SDL_SBPlaySegInIRQ(volatile byte *data,longword length)
{
//      int datapage;
        int dataofs;
        int uselen;

/*      uselen = length;
        if(uselen>4096) uselen=4096;

#ifdef BUFFERDMA                
        datapage=((int)data>>16)&255;
        dataofs=(int)data;
#else
        memcpy(DMABuffer[0],(byte *) data,uselen);
        datapage=((int)DMABuffer[0]>>16)&255;
//      dataofs=(int)DMABuffer[0];
#endif*/

        dataofs=inp(sbDMAa2);
        dataofs|=inp(sbDMAa2)<<8;
//      dataofs=inp(sbDMAa2)|(inp(sbDMAa2)<<8);
        int bufoffs=dataofs-(word)DMABuffer;
        int lenleft=8192-bufoffs;
        if(length>lenleft) uselen=lenleft;
        else uselen=length;
//      if(uselen>2048) uselen=2048;

        memcpy(DMABuffer+bufoffs,(byte *) data,uselen);

        uselen--;

/*      // Program the DMA controller
        outp(0x0a,sbDMA | 4);                                   // Mask off DMA on channel sbDMA
        outp(0x0c,0);                                                   // Clear byte ptr flip-flop to lower byte
        outp(0x0b,0x48 | sbDMA);                                // Set transfer mode for D/A conv
        outp(sbDMAa2,(byte)dataofs);                    // Give LSB of address
        outp(sbDMAa2,(byte)(dataofs >> 8));             // Give MSB of address
        outp(sbDMAa1,(byte)datapage);                   // Give page of address
        outp(sbDMAa3,(byte)uselen);                             // Give LSB of length
        outp(sbDMAa3,(byte)(uselen >> 8));              // Give MSB of length
        outp(0x0a,sbDMA);                                               // Re-enable DMA on channel sbDMA
*/

        // Start playing the thing

/*      sbWriteDelay();
        sbOut(sbWriteCmd,0x48);
        sbWriteDelay();
        sbOut(sbWriteData,(byte)uselen);
        sbWriteDelay();
        sbOut(sbWriteData,(byte)(uselen >> 8));
        sbWriteDelay();
        sbOut(sbWriteCmd,0x1c);*/

        sbWriteDelay();
        sbOut(sbWriteCmd,0x14);
        sbWriteDelay();
        sbOut(sbWriteData,(byte)uselen);
        sbWriteDelay();
        sbOut(sbWriteData,(byte)(uselen >> 8));
        return(uselen + 1);
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SBService() - Services the SoundBlaster DMA interrupt
//
///////////////////////////////////////////////////////////////////////////
static void __interrupt
SDL_SBService(void)
{
        longword        used;

        sbIn(sbDataAvail);      // Ack interrupt to SB

        if (sbNextSegPtr)
        {
                used = SDL_SBPlaySegInIRQ(sbNextSegPtr,sbNextSegLen);
                if (sbNextSegLen <= used)
                        sbNextSegPtr = nil;
                else
                {
                        sbNextSegPtr += used;
                        sbNextSegLen -= used;
                }
        }
        else
        {
                SDL_SBStopSampleInIRQ();
                SDL_DigitizedDoneInIRQ();
        }

        outp(0x20,0x20);        // Ack interrupt
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SBPlaySample() - Plays a sampled sound on the SoundBlaster. Sets up
//              DMA to play the sound
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_SBPlaySample(byte *data,longword len,boolean inIRQ)
{
        longword        used;

        if(!inIRQ)
        {
//              _asm    pushfd
                _asm    cli
        }

        SDL_SBStopSampleInIRQ();

        used = SDL_SBPlaySegInIRQ(data,len);    // interrupt flag already disabled
        if (len <= used)
                sbNextSegPtr = nil;
        else
        {
                sbNextSegPtr = data + used;
                sbNextSegLen = len - used;
        }

        // Save old interrupt status and unmask ours
        sbOldIntMask = inp(0x21);
        outp(0x21,sbOldIntMask & ~(1 << sbInterrupt));

/*
        sbWriteDelay();
        sbOut(sbWriteCmd,0xd4);                                         // Make sure DSP DMA is enabled
*/

        sbSamplePlaying = true;

        if(!inIRQ)
        {
//              _asm    popfd
                _asm    sti
        }

#ifdef SHOWSDDEBUG
        static int numplayed=0;
        numplayed++;
        VL_Plot(numplayed,1,14);
#endif
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_PositionSBP() - Sets the attenuation levels for the left and right
//              channels by using the mixer chip on the SB Pro. This hits a hole in
//              the address map for normal SBs.
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_PositionSBP(int leftpos,int rightpos)
{
        byte    v;

        if (!SBProPresent)
                return;

        leftpos = 15 - leftpos;
        rightpos = 15 - rightpos;
        v = ((leftpos & 0x0f) << 4) | (rightpos & 0x0f);

//      _asm    pushfd
        _asm    cli

        sbOut(sbpMixerAddr,sbpmVoiceVol);
        sbOut(sbpMixerData,v);

//      _asm    popfd
        _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_CheckSB() - Checks to see if a SoundBlaster resides at a
//              particular I/O location
//
///////////////////////////////////////////////////////////////////////////
static boolean
SDL_CheckSB(int port)
{
        int     i;

        sbLocation = port << 4;         // Initialize stuff for later use

        sbOut(sbReset,true);            // Reset the SoundBlaster DSP
        _asm {
                mov     edx,0x388                               // Wait >4usec
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
                in      al, dx
        }

        sbOut(sbReset,false);           // Turn off sb DSP reset

        _asm {
                mov     edx,0x388                               // Wait >100usec
                mov     ecx,100
usecloop:
                in      al,dx
                loop usecloop
        }

        for (i = 0;i < 100;i++)
        {
                if (sbIn(sbDataAvail) & 0x80)           // If data is available...
                {
                        if (sbIn(sbReadData) == 0xaa)   // If it matches correct value
                                return(true);
                        else
                        {
                                sbLocation = -1;                        // Otherwise not a SoundBlaster
                                return(false);
                        }
                }
        }
        sbLocation = -1;                                                // Retry count exceeded - fail
        return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//      Checks to see if a SoundBlaster is in the system. If the port passed is
//              -1, then it scans through all possible I/O locations. If the port
//              passed is 0, then it uses the default (2). If the port is >0, then
//              it just passes it directly to SDL_CheckSB()
//
///////////////////////////////////////////////////////////////////////////
static boolean
SDL_DetectSoundBlaster(int port)
{
        int     i;

        if (port == 0)                                  // If user specifies default, use 2
                port = 2;
        if (port == -1)
        {
                if (SDL_CheckSB(2))                     // Check default before scanning
                        return(true);

                if (SDL_CheckSB(4))                     // Check other SB Pro location before scan
                        return(true);

                for (i = 1;i <= 6;i++)          // Scan through possible SB locations
                {
                        if ((i == 2) || (i == 4))
                                continue;

                        if (SDL_CheckSB(i))             // If found at this address,
                                return(true);           //      return success
                }
                return(false);                          // All addresses failed, return failure
        }
        else
                return(SDL_CheckSB(port));      // User specified address or default
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SBSetDMA() - Sets the DMA channel to be used by the SoundBlaster
//              code. Sets up sbDMA, and sbDMAa1-sbDMAa3 (used by SDL_SBPlaySeg()).
//
///////////////////////////////////////////////////////////////////////////
void
SDL_SBSetDMA(byte channel)
{
        if (channel > 3)
                Quit("SDL_SBSetDMA() - invalid SoundBlaster DMA channel");

        sbDMA = channel;
        sbDMAa1 = sba1Vals[channel];
        sbDMAa2 = sba2Vals[channel];
        sbDMAa3 = sba3Vals[channel];
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_StartSB() - Turns on the SoundBlaster
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartSB(void)
{
        byte    timevalue,test;

        sbIntVec = sbIntVectors[sbInterrupt];
        if (sbIntVec < 0)
                Quit("SDL_StartSB: Illegal or unsupported interrupt number for SoundBlaster");

        byte *buffer;
        if(DPMI_GetDOSMemory((void **) &buffer, &DMABufferDescriptor,4*DMABUFFERSIZE)) // 2*(2*DMABUFFERSIZE)
                Quit("SDL_StartSB: Unable to allocate DMA buffer");

        if(((unsigned long) buffer & 0xffff) + (2*DMABUFFERSIZE) > 0x10000)
        {
                buffer = (byte *)(((unsigned long) buffer & 0xff0000) + 0x10000);
        }
        DMABuffer=buffer;
        memset(DMABuffer,128,2*DMABUFFERSIZE);

        sbOldIntHand = _dos_getvect(sbIntVec);  // Get old interrupt handler
        _dos_setvect(sbIntVec,SDL_SBService);   // Set mine

        sbWriteDelay();
        sbOut(sbWriteCmd,0xd1);                         // Turn on DSP speaker

        // Set the SoundBlaster DAC time constant for 7KHz
        timevalue = 256 - (1000000 / 7000);
        sbWriteDelay();
        sbOut(sbWriteCmd,0x40);
        sbWriteDelay();
        sbOut(sbWriteData,timevalue);

        // Program the DMA controller
        int datapage=((int)DMABuffer>>16)&255;
        int dataofs=(int)DMABuffer;

        outp(0x0a,sbDMA | 4);                                   // Mask off DMA on channel sbDMA
        outp(0x0c,0);                                                   // Clear byte ptr flip-flop to lower byte
        outp(0x0b,0x58 | sbDMA);                                // Set transfer mode for D/A conv
        outp(sbDMAa2,(byte)dataofs);                    // Give LSB of address
        outp(sbDMAa2,(byte)(dataofs >> 8));             // Give MSB of address
        outp(sbDMAa1,(byte)datapage);                   // Give page of address
        outp(sbDMAa3,(byte)(2*DMABUFFERSIZE-1));                                // Give LSB of length
        outp(sbDMAa3,(byte)((2*DMABUFFERSIZE-1) >> 8));         // Give MSB of length
        outp(0x0a,sbDMA);                                               // Re-enable DMA on channel sbDMA

        SBProPresent = false;
        if (sbNoProCheck)
                return;

        // Check to see if this is a SB Pro
        sbOut(sbpMixerAddr,sbpmFMVol);
        sbpOldFMMix = sbIn(sbpMixerData);
        sbOut(sbpMixerData,0xbb);
        test = sbIn(sbpMixerData);
        if (test == 0xbb)
        {
                // Boost FM output levels to be equivilent with digitized output
                sbOut(sbpMixerData,0xff);
                test = sbIn(sbpMixerData);
                if (test == 0xff)
                {
                        SBProPresent = true;

                        // Save old Voice output levels (SB Pro)
                        sbOut(sbpMixerAddr,sbpmVoiceVol);
                        sbpOldVOCMix = sbIn(sbpMixerData);

                        // Turn SB Pro stereo DAC off
                        sbOut(sbpMixerAddr,sbpmControl);
                        sbOut(sbpMixerData,0);                          // 0=off,2=on
                }
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ShutSB() - Turns off the SoundBlaster
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutSB(void)
{
        _asm cli
        SDL_SBStopSampleInIRQ();
        _asm sti

        if (SBProPresent)
        {
                // Restore FM output levels (SB Pro)
                sbOut(sbpMixerAddr,sbpmFMVol);
                sbOut(sbpMixerData,sbpOldFMMix);

                // Restore Voice output levels (SB Pro)
                sbOut(sbpMixerAddr,sbpmVoiceVol);
                sbOut(sbpMixerData,sbpOldVOCMix);
        }

        _dos_setvect(sbIntVec,sbOldIntHand);            // Set vector back

        DPMI_FreeDOSMemory(DMABufferDescriptor);
}

//      Sound Source Code

///////////////////////////////////////////////////////////////////////////
//
//      SDL_SSStopSample() - Stops a sample playing on the Sound Source
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_SSStopSampleInIRQ(void)
{
        ssSample = 0;
}


///////////////////////////////////////////////////////////////////////////
//
//      SDL_SSPlaySample() - Plays the specified sample on the Sound Source
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_SSPlaySample(byte *data,longword len,boolean inIRQ)
{
        if(!inIRQ)
        {
//              _asm    pushfd
                _asm    cli
        }

        ssLengthLeft = len;
        ssSample = (volatile byte *)data;

        if(!inIRQ)
        {
//              _asm    popfd
                _asm    sti
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_StartSS() - Sets up for and turns on the Sound Source
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartSS(void)
{
        if (ssPort == 3)
                ssControl = 0x27a;      // If using LPT3
        else if (ssPort == 2)
                ssControl = 0x37a;      // If using LPT2
        else
                ssControl = 0x3be;      // If using LPT1
        ssStatus = ssControl - 1;
        ssData = ssStatus - 1;

        ssOn = 0x04;
        if (ssIsTandy)
                ssOff = 0x0e;                           // Tandy wierdness
        else
                ssOff = 0x0c;                           // For normal machines

        outp(ssControl,ssOn);           // Enable SS
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ShutSS() - Turns off the Sound Source
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutSS(void)
{
        outp(ssControl,ssOff);
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_CheckSS() - Checks to see if a Sound Source is present at the
//              location specified by the sound source variables
//
///////////////////////////////////////////////////////////////////////////
static boolean
SDL_CheckSS(void)
{
        boolean         present = false;
        longword        lasttime;

        // Turn the Sound Source on and wait awhile (4 ticks)
        SDL_StartSS();

        lasttime = TimeCount;
        while (TimeCount < lasttime + 4)
                ;

        if(inp(ssStatus)&0x40) goto checkdone;          // Check to see if FIFO is currently empty

        _asm {
                mov             ecx,32                  // Force FIFO overflow (FIFO is 16 bytes)
outloop:
                mov             dx,[ssData]             // Pump a neutral value out
                mov             al,0x80
                out             dx,al

                mov             dx,[ssControl]  // Pulse printer select
                mov             al,[ssOff]
                out             dx,al
                push            eax
                pop             eax
                mov             al,[ssOn]
                out             dx,al

                push            eax                             // Delay a short while before we do this again
                pop             eax
                push            eax
                pop             eax

                loop    outloop
        }

        if(inp(ssStatus)&0x40) present=true; // Is FIFO overflowed now?
        
checkdone:

        SDL_ShutSS();
        return(present);
}

static boolean
SDL_DetectSoundSource(void)
{
        for (ssPort = 1;ssPort <= 3;ssPort++)
                if (SDL_CheckSS())
                        return(true);
        return(false);
}

//
//      PC Sound code
//

///////////////////////////////////////////////////////////////////////////
//
//      SDL_PCPlaySample() - Plays the specified sample on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_PCPlaySample(byte *data,longword len,boolean inIRQ)
{
        if(!inIRQ)
        {
//              _asm    pushfd
                _asm    cli
        }

        SDL_IndicatePC(true);

        pcLengthLeft = len;
        pcSound = (volatile byte *)data;

        if(!inIRQ)
        {
//              _asm    popfd
                _asm    sti
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_PCStopSample() - Stops a sample playing on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_PCStopSampleInIRQ(void)
{
        pcSound = 0;

        SDL_IndicatePC(false);

        _asm    in      al,0x61                 // Turn the speaker off
        _asm    and     al,0xfd                 // ~2
        _asm    out     0x61,al
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_PCPlaySound() - Plays the specified sound on the PC speaker
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_PCPlaySound(PCSound *sound)
{
//      _asm    pushfd
        _asm    cli

        pcLastSample = -1;
        pcLengthLeft = sound->common.length;
        pcSound = sound->data;

//      _asm    popfd
        _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_PCStopSound() - Stops the current sound playing on the PC Speaker
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_PCStopSound(void)
{
//      _asm    pushfd
        _asm    cli

        pcSound = 0;

        _asm    in      al,0x61                 // Turn the speaker off
        _asm    and     al,0xfd                 // ~2
        _asm    out     0x61,al

//      _asm    popfd
        _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ShutPC() - Turns off the pc speaker
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutPC(void)
{
//      _asm    pushfd
        _asm    cli

        pcSound = 0;

        _asm    in      al,0x61                 // Turn the speaker & gate off
        _asm    and     al,0xfc                 // ~3
        _asm    out     0x61,al

//      _asm    popfd
        _asm    sti
}

void
SDL_PlayDigiSegment(memptr addr,word len,boolean inIRQ)
{
        lastdigisegstart=(int)addr;
        switch (DigiMode)
        {
        case sds_PC:
        SDL_PCPlaySample((byte *) addr,len,inIRQ);
                break;
        case sds_SoundSource:
                SDL_SSPlaySample((byte *) addr,len,inIRQ);
                break;
        case sds_SoundBlaster:
                SDL_SBPlaySample((byte *) addr,len,inIRQ);
                break;
        }
}

void
SD_StopDigitized(void)
{
//      _asm    pushfd
        _asm    cli

        DigiLeft = 0;
        DigiNextAddr = nil;
        DigiNextLen = 0;
        DigiMissed = false;
        DigiPlaying = false;
        DigiNumber = (soundnames) 0;
        DigiPriority = 0;
        SoundPositioned = false;
        if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
                SDL_SoundFinished();

        switch (DigiMode)
        {
        case sds_PC:
                SDL_PCStopSampleInIRQ();
                break;
        case sds_SoundSource:
                SDL_SSStopSampleInIRQ();
                break;
        case sds_SoundBlaster:
                SDL_SBStopSampleInIRQ();
                break;
        }

//      _asm    popfd
        _asm    sti

        DigiLastStart = 1;
        DigiLastEnd = 0;
}

void
SD_Poll(void)
{
        if (DigiLeft && !DigiNextAddr)
        {
                DigiNextLen = (DigiLeft >= PMPageSize)? PMPageSize : (DigiLeft % PMPageSize);
                DigiLeft -= DigiNextLen;
                if (!DigiLeft)
                        DigiLastSegment = true;
                DigiNextAddr = SDL_LoadDigiSegment(DigiPage++);
#ifdef BUFFERDMA
                if(DigiMode==sds_SoundBlaster)
                {
                        DMABufferIndex=(DMABufferIndex+1)&1;
                        memcpy(DMABuffer[DMABufferIndex],DigiNextAddr,DigiNextLen);
                        DigiNextAddr=DMABuffer[DMABufferIndex];
                }
#endif
        }
        if (DigiMissed && DigiNextAddr)
        {
#ifdef SHOWSDDEBUG
                static int nummissed=0;
                nummissed++;
                VL_Plot(nummissed,0,12);
#endif
                
                SDL_PlayDigiSegment(DigiNextAddr,DigiNextLen,false);
                DigiNextAddr = nil;
                DigiMissed = false;
                if (DigiLastSegment)
                {
                        DigiPlaying = false;
                        DigiLastSegment = false;
                }
        }
        SDL_SetTimerSpeed();
}

void
SD_SetPosition(int leftpos,int rightpos)
{
        if      ((leftpos < 0) ||       (leftpos > 15)  ||      (rightpos < 0)  ||      (rightpos > 15)
                        ||      ((leftpos == 15) && (rightpos == 15)))
                Quit("SD_SetPosition: Illegal position");

        switch (DigiMode)
        {
        case sds_SoundBlaster:
                SDL_PositionSBP(leftpos,rightpos);
                break;
        }
}

void
SD_PlayDigitized(word which,int leftpos,int rightpos)
{
        word    len;
        memptr  addr;

        if (!DigiMode)
                return;

        SD_StopDigitized();
        if (which >= NumDigi)
                Quit("SD_PlayDigitized: bad sound number");

        SD_SetPosition(leftpos,rightpos);

        lastdigiwhich=which;

        DigiPage = DigiList[(which * 2) + 0];
        DigiLeft = DigiList[(which * 2) + 1];

        lastdigistart=DigiPage;

        DigiLastStart = DigiPage;
        DigiLastEnd = DigiPage + ((DigiLeft + (PMPageSize - 1)) / PMPageSize);

#ifdef NOSEGMENTATION
        len = DigiLeft;
#else
        len = (DigiLeft >= PMPageSize)? PMPageSize : (DigiLeft % PMPageSize);
#endif
        addr = SDL_LoadDigiSegment(DigiPage++);

#ifdef BUFFERDMA
        if(DigiMode==sds_SoundBlaster)
        {
                DMABufferIndex=(DMABufferIndex+1)&1;
                memcpy(DMABuffer[DMABufferIndex],addr,len);
                addr=DMABuffer[DMABufferIndex];
        }
#endif

        DigiPlaying = true;
        DigiLastSegment = false;

        SDL_PlayDigiSegment(addr,len,false);
        DigiLeft -= len;
        if (!DigiLeft)
                DigiLastSegment = true;

        SD_Poll();
}

void
SDL_DigitizedDoneInIRQ(void)
{
        if (DigiNextAddr)
        {
                SDL_PlayDigiSegment(DigiNextAddr,DigiNextLen,true);
                DigiNextAddr = nil;
                DigiMissed = false;
        }
        else
        {
                if (DigiLastSegment)
                {
                        DigiPlaying = false;
                        DigiLastSegment = false;
                        if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
                        {
                                SDL_SoundFinished();
                        }
                        else
                        {
                                DigiNumber = (soundnames) 0;
                                DigiPriority = 0;
                        }
                        SoundPositioned = false;
                }
                else
                        DigiMissed = true;
        }
}

void
SD_SetDigiDevice(SDSMode mode)
{
        boolean devicenotpresent;

        if (mode == DigiMode)
                return;

        SD_StopDigitized();

        devicenotpresent = false;
        switch (mode)
        {
        case sds_SoundBlaster:
                if (!SoundBlasterPresent)
                {
                        if (SoundSourcePresent)
                                mode = sds_SoundSource;
                        else
                                devicenotpresent = true;
                }
                break;
        case sds_SoundSource:
                if (!SoundSourcePresent)
                        devicenotpresent = true;
                break;
        }

        if (!devicenotpresent)
        {
                if (DigiMode == sds_SoundSource)
                        SDL_ShutSS();

                DigiMode = mode;

                if (mode == sds_SoundSource)
                        SDL_StartSS();

                SDL_SetTimerSpeed();
        }
}

void
SDL_SetupDigi(void)
{
        memptr  list;
        word    *p;
        word pg;
        int             i;

        list=malloc(PMPageSize);
        p=(word *)(Pages+((ChunksInFile-1)<<12));
        memcpy(list,p,PMPageSize);
        
        pg = PMSoundStart;
        for (i = 0;i < PMPageSize / (sizeof(word) * 2);i++,p += 2)
        {
                if (pg >= ChunksInFile - 1)
                        break;
                pg += (p[1] + (PMPageSize - 1)) / PMPageSize;
        }
        
        DigiList=(word *) malloc(i*sizeof(word)*2);
        memcpy(DigiList,list,i*sizeof(word)*2);
        free(list);
        
        NumDigi = i;

        for (i = 0;i < LASTSOUND;i++)
                DigiMap[i] = -1;
}

//      AdLib Code

///////////////////////////////////////////////////////////////////////////
//
//      alOut(n,b) - Puts b in AdLib card register n
//
///////////////////////////////////////////////////////////////////////////
void
alOut(byte n,byte b)
{
        _asm {
//              pushfd
                cli

                mov     dx,0x388
                mov     al,[n]
                out     dx,al
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                inc     dx
                mov     al,[b]
                out     dx,al

//              popfd
                sti

                dec     dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
        }
}

// Inside an interrupt handler interrupts should already be disabled
// so don't disable them again and cause V86 exceptions which cost
// aprox. 300 processor tics!

static void alOutInIRQ(byte n,byte b)
{
        _asm {
                mov     dx,0x388
                mov     al,[n]
                out     dx,al
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                inc     dx
                mov     al,[b]
                out     dx,al

                dec     dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx

                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
                in      al,dx
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ALStopSound() - Turns off any sound effects playing through the
//              AdLib card
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_ALStopSound(void)
{
//      _asm    cli

        alNoIRQ = true;
        
        alSound = 0;
        alOutInIRQ(alFreqH + 0,0);

        alNoIRQ = false;

//      _asm    sti
}

static void
SDL_AlSetFXInst(Instrument *inst)
{
        byte            c,m;

        m = modifiers[0];
        c = carriers[0];
        alOutInIRQ(m + alChar,inst->mChar);
        alOutInIRQ(m + alScale,inst->mScale);
        alOutInIRQ(m + alAttack,inst->mAttack);
        alOutInIRQ(m + alSus,inst->mSus);
        alOutInIRQ(m + alWave,inst->mWave);
        alOutInIRQ(c + alChar,inst->cChar);
        alOutInIRQ(c + alScale,inst->cScale);
        alOutInIRQ(c + alAttack,inst->cAttack);
        alOutInIRQ(c + alSus,inst->cSus);
        alOutInIRQ(c + alWave,inst->cWave);

        // Note: Switch commenting on these lines for old MUSE compatibility
//      alOutInIRQ(alFeedCon,inst->nConn);
        alOutInIRQ(alFeedCon,0);
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ALPlaySound() - Plays the specified sound on the AdLib card
//
///////////////////////////////////////////////////////////////////////////
#ifdef  _MUSE_
void
#else
static void
#endif
SDL_ALPlaySound(AdLibSound *sound)
{
        Instrument      *inst;
        byte            *data;

        SDL_ALStopSound();

//      _asm    cli
        alNoIRQ = true;

        alLengthLeft = sound->common.length;
        data = sound->data;
        alSound = (byte *)data;
        alBlock = ((sound->block & 7) << 2) | 0x20;
        inst = &sound->inst;

        if (!(inst->mSus | inst->cSus))
        {
//              _asm    sti
                Quit("SDL_ALPlaySound() - Bad instrument");
        }

//      SDL_AlSetFXInst(&alZeroInst);   // DEBUG
        SDL_AlSetFXInst(inst);

        alNoIRQ = false;

//      _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_ShutAL() - Shuts down the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_ShutAL(void)
{
//      _asm    cli
        alNoIRQ = true;

        alOutInIRQ(alEffects,0);
        alOutInIRQ(alFreqH + 0,0);
        SDL_AlSetFXInst(&alZeroInst);
        alSound = 0;

        alNoIRQ = false;
//      _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_CleanAL() - Totally shuts down the AdLib card
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_CleanAL(void)
{
        int     i;

//      _asm    cli

        alNoIRQ = true;

        alOutInIRQ(alEffects,0);
        for (i = 1;i < 0xf5;i++)
                alOutInIRQ(i,0);

        alNoIRQ = false;
//      _asm    sti
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_StartAL() - Starts up the AdLib card for sound effects
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartAL(void)
{
        alNoIRQ = true;
        alFXReg = 0;
        alOutInIRQ(alEffects,alFXReg);
        SDL_AlSetFXInst(&alZeroInst);
        alNoIRQ = false;
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_DetectAdLib() - Determines if there's an AdLib (or SoundBlaster
//              emulating an AdLib) present
//
///////////////////////////////////////////////////////////////////////////
static boolean
SDL_DetectAdLib(void)
{
        byte    status1,status2;
        int             i;

        alOutInIRQ(4,0x60);     // Reset T1 & T2
        alOutInIRQ(4,0x80);     // Reset IRQ
        status1 = readstat();
        alOutInIRQ(2,0xff);     // Set timer 1
        alOutInIRQ(4,0x21);     // Start timer 1
#if 0
        SDL_Delay(TimerDelay100);
#else
        _asm {
                mov     edx,0x388
                mov     ecx,100
usecloop:
                in      al,dx
                loop usecloop
        }
#endif

        status2 = readstat();
        alOutInIRQ(4,0x60);
        alOutInIRQ(4,0x80);

        if (((status1 & 0xe0) == 0x00) && ((status2 & 0xe0) == 0xc0))
        {
                for (i = 1;i <= 0xf5;i++)       // Zero all the registers
                        alOutInIRQ(i,0);

                alOutInIRQ(1,0x20);     // Set WSE=1
                alOutInIRQ(8,0);                // Set CSM=0 & SEL=0

                return(true);
        }
        else
                return(false);
}

////////////////////////////////////////////////////////////////////////////
//
//      SDL_ShutDevice() - turns off whatever device was being used for sound fx
//
////////////////////////////////////////////////////////////////////////////
static void
SDL_ShutDevice(void)
{
        switch (SoundMode)
        {
        case sdm_PC:
                SDL_ShutPC();
                break;
        case sdm_AdLib:
                SDL_ShutAL();
                break;
        }
        SoundMode = sdm_Off;
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_CleanDevice() - totally shuts down all sound devices
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_CleanDevice(void)
{
        if ((SoundMode == sdm_AdLib) || (MusicMode == smm_AdLib))
                SDL_CleanAL();
}

///////////////////////////////////////////////////////////////////////////
//
//      SDL_StartDevice() - turns on whatever device is to be used for sound fx
//
///////////////////////////////////////////////////////////////////////////
static void
SDL_StartDevice(void)
{
        switch (SoundMode)
        {
        case sdm_AdLib:
                SDL_StartAL();
                break;
        }
        SoundNumber = (soundnames) 0;
        SoundPriority = 0;
}

//      Public routines

///////////////////////////////////////////////////////////////////////////
//
//      SD_SetSoundMode() - Sets which sound hardware to use for sound effects
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetSoundMode(SDMode mode)
{
        boolean result = false;
        word    tableoffset;

        SD_StopSound();

#ifndef _MUSE_
        if ((mode == sdm_AdLib) && !AdLibPresent)
                mode = sdm_PC;

        switch (mode)
        {
        case sdm_Off:
                NeedsDigitized = false;
                result = true;
                break;
        case sdm_PC:
                tableoffset = STARTPCSOUNDS;
                NeedsDigitized = false;
                result = true;
                break;
        case sdm_AdLib:
                if (AdLibPresent)
                {
                        tableoffset = STARTADLIBSOUNDS;
                        NeedsDigitized = false;
                        result = true;
                }
                break;
        }
#else
        result = true;
#endif

        if (result && (mode != SoundMode))
        {
                SDL_ShutDevice();
                SoundMode = mode;
#ifndef _MUSE_
                SoundTable = &audiosegs[tableoffset];
#endif
                SDL_StartDevice();
        }

        SDL_SetTimerSpeed();

        return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_SetMusicMode() - sets the device to use for background music
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_SetMusicMode(SMMode mode)
{
        boolean result = false;

        SD_FadeOutMusic();
        while (SD_MusicPlaying())
                ;

        switch (mode)
        {
        case smm_Off:
                NeedsMusic = false;
                result = true;
                break;
        case smm_AdLib:
                if (AdLibPresent)
                {
                        NeedsMusic = true;
                        result = true;
                }
                break;
        }

        if (result)
                MusicMode = mode;

        SDL_SetTimerSpeed();

        return(result);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_Startup() - starts up the Sound Mgr
//              Detects all additional sound hardware and installs my ISR
//
///////////////////////////////////////////////////////////////////////////
void
SD_Startup(void)
{
        int     i;

        if (SD_Started)
                return;

        ssIsTandy = false;
        ssNoCheck = false;
        alNoCheck = false;
        sbNoCheck = false;
        sbNoProCheck = false;
#ifndef _MUSE_
        for (i = 1;i < __argc;i++)
        {
                switch (US_CheckParm(__argv[i],ParmStrings))
                {
                case 0:                                         // No AdLib detection
                        alNoCheck = true;
                        break;
                case 1:                                         // No SoundBlaster detection
                        sbNoCheck = true;
                        break;
                case 2:                                         // No SoundBlaster Pro detection
                        sbNoProCheck = true;
                        break;
                case 3:
                        ssNoCheck = true;               // No Sound Source detection
                        break;
                case 4:                                         // Tandy Sound Source handling
                        ssIsTandy = true;
                        break;
                case 5:                                         // Sound Source present at LPT1
                        ssPort = 1;
                        ssNoCheck = SoundSourcePresent = true;
                        break;
                case 6:                     // Sound Source present at LPT2
                        ssPort = 2;
                        ssNoCheck = SoundSourcePresent = true;
                        break;
                case 7:                     // Sound Source present at LPT3
                        ssPort = 3;
                        ssNoCheck = SoundSourcePresent = true;
                        break;
                }
        }
#endif

//        SoundUserHook = 0;

        t0OldService = _dos_getvect(8); // Get old timer 0 ISR

        LocalTime = TimeCount = alTimeCount = 0;

        SD_SetSoundMode(sdm_Off);
        SD_SetMusicMode(smm_Off);

        if (!ssNoCheck)
                SoundSourcePresent = SDL_DetectSoundSource();

        if (!alNoCheck)
        {
                AdLibPresent = SDL_DetectAdLib();
                if (AdLibPresent && !sbNoCheck)
                {
                        int port = -1;
                        char *env = getenv("BLASTER");
                        if (env)
                        {
                                long temp;
                                while (*env)
                                {
                                        while (isspace(*env))
                                                env++;

                                        switch (toupper(*env))
                                        {
                                        case 'A':
                                                temp = strtol(env + 1,&env,16);
                                                if
                                                (
                                                        (temp >= 0x210)
                                                &&      (temp <= 0x260)
                                                &&      (!(temp & 0x00f))
                                                )
                                                        port = (temp - 0x200) >> 4;
                                                else
                                                        Quit("SD_Startup: Unsupported address value in BLASTER");
                                                break;
                                        case 'I':
                                                temp = strtol(env + 1,&env,10);
                                                if
                                                (
                                                        (temp >= 0)
                                                &&      (temp <= 10)
                                                &&      (sbIntVectors[temp] != -1)
                                                )
                                                {
                                                        sbInterrupt = temp;
                                                        sbIntVec = sbIntVectors[sbInterrupt];
                                                }
                                                else
                                                        Quit("SD_Startup: Unsupported interrupt value in BLASTER");
                                                break;
                                        case 'D':
                                                temp = strtol(env + 1,&env,10);
                                                if ((temp == 0) || (temp == 1) || (temp == 3))
                                                        SDL_SBSetDMA((byte)temp);
                                                else
                                                        Quit("SD_Startup: Unsupported DMA value in BLASTER");
                                                break;
                                        default:
                                                while (isspace(*env))
                                                        env++;
                                                while (*env && !isspace(*env))
                                                        env++;
                                                break;
                                        }
                                }
                        }
                        SoundBlasterPresent = SDL_DetectSoundBlaster(port);
                }
        }

        if (SoundBlasterPresent)
                SDL_StartSB();

        SDL_SetupDigi();

        SD_Started = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_Default() - Sets up the default behaviour for the Sound Mgr whether
//              the config file was present or not.
//
///////////////////////////////////////////////////////////////////////////
/* void
SD_Default(boolean gotit,SDMode sd,SMMode sm)
{
        boolean gotsd,gotsm;

        gotsd = gotsm = gotit;

        if (gotsd)      // Make sure requested sound hardware is available
        {
                switch (sd)
                {
                case sdm_AdLib:
                        gotsd = AdLibPresent;
                        break;
                }
        }
        if (!gotsd)
        {
                if (AdLibPresent)
                        sd = sdm_AdLib;
                else
                        sd = sdm_PC;
        }
        if (sd != SoundMode)
                SD_SetSoundMode(sd);


        if (gotsm)      // Make sure requested music hardware is available
        {
                switch (sm)
                {
                case sdm_AdLib:
                        gotsm = AdLibPresent;
                        break;
                }
        }
        if (!gotsm)
        {
                if (AdLibPresent)
                        sm = smm_AdLib;
        }
        if (sm != MusicMode)
                SD_SetMusicMode(sm);
} */

///////////////////////////////////////////////////////////////////////////
//
//      SD_Shutdown() - shuts down the Sound Mgr
//              Removes sound ISR and turns off whatever sound hardware was active
//
///////////////////////////////////////////////////////////////////////////
void
SD_Shutdown(void)
{
        if (!SD_Started)
                return;

        SD_MusicOff();
        SD_StopSound();
        SDL_ShutDevice();
        SDL_CleanDevice();

        if (SoundBlasterPresent)
                SDL_ShutSB();

        if (SoundSourcePresent)
                SDL_ShutSS();

//      _asm    pushfd
        _asm    cli

        SDL_SetTimer0(0);

        _dos_setvect(8,t0OldService);

//      _asm    popfd
        _asm    sti

        SD_Started = false;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_SetUserHook() - sets the routine that the Sound Mgr calls every 1/70th
//              of a second from its timer 0 ISR
//
///////////////////////////////////////////////////////////////////////////
/* void
SD_SetUserHook(void (* hook)(void))
{
        SoundUserHook = hook;
} */

///////////////////////////////////////////////////////////////////////////
//
//      SD_PositionSound() - Sets up a stereo imaging location for the next
//              sound to be played. Each channel ranges from 0 to 15.
//
///////////////////////////////////////////////////////////////////////////
void
SD_PositionSound(int leftvol,int rightvol)
{
        LeftPosition = leftvol;
        RightPosition = rightvol;
        nextsoundpos = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_PlaySound() - plays the specified sound on the appropriate hardware
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_PlaySound(soundnames sound)
{
        boolean         ispos;
        SoundCommon     *s;
        int     lp,rp;


        lp = LeftPosition;
        rp = RightPosition;
        LeftPosition = 0;
        RightPosition = 0;

        ispos = nextsoundpos;
        nextsoundpos = false;

        if (sound == -1)
                return(false);

        s = (SoundCommon *) SoundTable[sound];

/*      gamestate.score=1000000;   // was used to test for invalid caching
        if (!(unsigned)SoundTable[17]) gamestate.score += 100000;
        if (!(unsigned)SoundTable[18]) gamestate.score += 10000;
        if (!(unsigned)SoundTable[19]) gamestate.score += 1000;
        if (!(unsigned)SoundTable[20]) gamestate.score += 100;
        if (!(unsigned)SoundTable[21]) gamestate.score += 10;
        if (!(unsigned)SoundTable[22]) gamestate.score += 1;
        DrawScore(); */
        
        if ((SoundMode != sdm_Off) && !s)
                Quit("SD_PlaySound() - Uncached sound");

        if ((DigiMode != sds_Off) && (DigiMap[sound] != -1))
        {
                if ((DigiMode == sds_PC) && (SoundMode == sdm_PC))
                {
                        if (s->priority < SoundPriority)
                                return(false);

                        SDL_PCStopSound();

                        SD_PlayDigitized(DigiMap[sound],lp,rp);
                        SoundPositioned = ispos;
                        SoundNumber = sound;
                        SoundPriority = s->priority;
                }
                else
                {
/*//                    _asm    pushfd
                        _asm    cli
                        if (DigiPriority && !DigiNumber)
                        {
//                              _asm    popfd
                                _asm    sti
                                Quit("SD_PlaySound: Priority without a sound");
                        }
//                      _asm    popfd
                        _asm    sti*/

                        if (s->priority < DigiPriority)
                                return(false);

                        lastsoundstarted=sound;

                        SD_PlayDigitized(DigiMap[sound],lp,rp);
                        SoundPositioned = ispos;
                        DigiNumber = sound;
                        DigiPriority = s->priority;
                }

                return(true);
        }

        if (SoundMode == sdm_Off)
                return(false);

        if (!s->length)
                Quit("SD_PlaySound() - Zero length sound");
        if (s->priority < SoundPriority)
                return(false);

        switch (SoundMode)
        {
        case sdm_PC:
                SDL_PCPlaySound((PCSound *)s);
                break;
        case sdm_AdLib:
                SDL_ALPlaySound((AdLibSound *)s);
                break;
        }

        SoundNumber = sound;
        SoundPriority = s->priority;

        return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_SoundPlaying() - returns the sound number that's playing, or 0 if
//              no sound is playing
//
///////////////////////////////////////////////////////////////////////////
word
SD_SoundPlaying(void)
{
        boolean result = false;

        switch (SoundMode)
        {
        case sdm_PC:
                result = pcSound? true : false;
                break;
        case sdm_AdLib:
                result = alSound? true : false;
                break;
        }

        if (result)
                return(SoundNumber);
        else
                return(false);
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_StopSound() - if a sound is playing, stops it
//
///////////////////////////////////////////////////////////////////////////
void
SD_StopSound(void)
{
        if (DigiPlaying)
                SD_StopDigitized();

        switch (SoundMode)
        {
        case sdm_PC:
                SDL_PCStopSound();
                break;
        case sdm_AdLib:
                SDL_ALStopSound();
                break;
        }

        SoundPositioned = false;

        SDL_SoundFinished();
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_WaitSoundDone() - waits until the current sound is done playing
//
///////////////////////////////////////////////////////////////////////////
void
SD_WaitSoundDone(void)
{
        while (SD_SoundPlaying())
                ;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicOn() - turns on the sequencer
//
///////////////////////////////////////////////////////////////////////////
void
SD_MusicOn(void)
{
        sqActive = true;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicOff() - turns off the sequencer and any playing notes
//      returns the last music offset for music continue
//
///////////////////////////////////////////////////////////////////////////
int
SD_MusicOff(void)
{
        word    i;

        alNoIRQ = true;

        sqActive = false;
        switch (MusicMode)
        {
        case smm_AdLib:
                alFXReg = 0;
                alOutInIRQ(alEffects,0);
                for (i = 0;i < sqMaxTracks;i++)
                        alOutInIRQ(alFreqH + i + 1,0);
                break;
        }

        alNoIRQ = false;
        
        return sqHackPtr-sqHack;
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_StartMusic() - starts playing the music pointed to
//
///////////////////////////////////////////////////////////////////////////
void
SD_StartMusic(MusicGroup *music)
{
        SD_MusicOff();

        if (MusicMode == smm_AdLib)
        {
                sqHackPtr = sqHack = music->values;
                sqHackLen = sqHackSeqLen = music->length;
                sqHackTime = 0;
                alTimeCount = 0;
                SD_MusicOn();
        }
}

void
SD_ContinueMusic(MusicGroup *music,int startoffs)
{
        SD_MusicOff();

        if (MusicMode == smm_AdLib)
        {
                sqHackPtr = sqHack = music->values;
                sqHackLen = sqHackSeqLen = music->length;

                if(startoffs >= sqHackLen)
                {
                        Quit("SD_StartMusic: Illegal startoffs provided!");
                }

                // fast forward to correct position
                // (needed to reconstruct the instruments)
                
                for(int i=0;i<startoffs;i+=2)
                {
                        byte reg=*(byte *)sqHackPtr;
                        byte val=*(((byte *)sqHackPtr)+1);
                        if(reg>=0xb1 && reg<=0xb8) val&=0xdf;           // disable play note flag
                        else if(reg==0xbd) val&=0xe0;                                   // disable drum flags
                        
//                      sqHackTime=alTimeCount+*(sqHackPtr+1);
                        alOutInIRQ(reg,val);
                        sqHackPtr+=2;
                        sqHackLen-=4;
                }
                sqHackTime = 0;
                alTimeCount = 0;
                
                SD_MusicOn();
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_FadeOutMusic() - starts fading out the music. Call SD_MusicPlaying()
//              to see if the fadeout is complete
//
///////////////////////////////////////////////////////////////////////////
void
SD_FadeOutMusic(void)
{
        switch (MusicMode)
        {
        case smm_AdLib:
                // DEBUG - quick hack to turn the music off
                SD_MusicOff();
                break;
        }
}

///////////////////////////////////////////////////////////////////////////
//
//      SD_MusicPlaying() - returns true if music is currently playing, false if
//              not
//
///////////////////////////////////////////////////////////////////////////
boolean
SD_MusicPlaying(void)
{
        boolean result;

        switch (MusicMode)
        {
        case smm_AdLib:
                result = false;
                // DEBUG - not written
                break;
        default:
                result = false;
        }

        return(result);
}
