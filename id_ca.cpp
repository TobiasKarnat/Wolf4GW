// ID_CA.C

// this has been customized for WOLF

/*
=============================================================================

Id Software Caching Manager
---------------------------

Must be started BEFORE the memory manager, because it needs to get the headers
loaded into the data segment

=============================================================================
*/

#pragma hdrstop

#pragma warn -pro
#pragma warn -use

#define THREEBYTEGRSTARTS

/*
=============================================================================

                                                 LOCAL CONSTANTS

=============================================================================
*/

typedef struct
{
  word bit0,bit1;       // 0-255 is a character, > is a pointer to a node
} huffnode;


typedef struct
{
        word RLEWtag;
        long            headeroffsets[100];
        byte            tileinfo[];
} mapfiletype;


/*
=============================================================================

                                                 GLOBAL VARIABLES

=============================================================================
*/

byte bufferseg[BUFFERSIZE];

int                     mapon;

word *mapsegs[MAPPLANES];
maptype *mapheaderseg[NUMMAPS];
byte *audiosegs[NUMSNDCHUNKS];
byte *grsegs[NUMCHUNKS];

byte grneeded[NUMCHUNKS];

word RLEWtag;

char            audioname[13]="AUDIO.";

/*
=============================================================================

                                                 LOCAL VARIABLES

=============================================================================
*/

char extension[5],      // Need a string, not constant to change cache files
     gheadname[10]="VGAHEAD.",
     gfilename[10]="VGAGRAPH.",
     gdictname[10]="VGADICT.",
     mheadname[10]="MAPHEAD.",
     mfilename[10]="MAPTEMP.",
     aheadname[10]="AUDIOHED.",
     afilename[10]="AUDIOT.";

void CA_CannotOpen(char *string);

byte grstarts[(NUMCHUNKS+1)*3];
long            *audiostarts;   // array of offsets in audio / audiot

#ifdef GRHEADERLINKED
huffnode        *grhuffman;
#else
huffnode        grhuffman[255];
#endif

/*
#ifdef AUDIOHEADERLINKED                // not used in wolf3d
huffnode        *audiohuffman;
#else
huffnode        audiohuffman[255];
#endif
*/

int                     grhandle;               // handle to EGAGRAPH
int                     maphandle;              // handle to MAPTEMP / GAMEMAPS
int                     audiohandle;    // handle to AUDIOT / AUDIO

long            chunkcomplen,chunkexplen;

SDMode          oldsoundmode;


#ifdef THREEBYTEGRSTARTS
#define FILEPOSSIZE     3
//#define       GRFILEPOS(c) (*(long far *)(((byte far *)grstarts)+(c)*3)&0xffffff)
long GRFILEPOS(int c)
{
        long value;
        int     offset;

        offset = c*3;

        value = *(long *)(grstarts+offset);

        value &= 0x00ffffffl;

        if (value == 0xffffffl)
                value = -1;

        return value;
};
#else
#define FILEPOSSIZE     4
#define GRFILEPOS(c) (grstarts[c])
#endif

/*
=============================================================================

                                           LOW LEVEL ROUTINES

=============================================================================
*/

/*
============================
=
= CAL_GetGrChunkLength
=
= Gets the length of an explicit length chunk (not tiles)
= The file pointer is positioned so the compressed data can be read in next.
=
============================
*/

void CAL_GetGrChunkLength (int chunk)
{
        lseek(grhandle,GRFILEPOS(chunk),SEEK_SET);
        read(grhandle,&chunkexplen,sizeof(chunkexplen));
        chunkcomplen = GRFILEPOS(chunk+1)-GRFILEPOS(chunk)-4;
}

/*
==========================
=
= CA_ReadFile
=
= Reads a file into an allready allocated buffer
=
==========================
*/

boolean CA_ReadFile (char *filename, memptr *ptr)
{
        int handle;
        long size;

        if ((handle = open(filename,O_RDONLY | O_BINARY, S_IREAD)) == -1)
                return false;

        size = filelength (handle);
        if (!read (handle,*ptr,size))
        {
                close (handle);
                return false;
        }
        close (handle);
        return true;
}


/*
==========================
=
= CA_WriteFile
=
= Writes a file from a memory buffer
=
==========================
*/

boolean CA_WriteFile (char *filename, void *ptr, long length)
{
        int handle;

        handle = open(filename,O_CREAT | O_BINARY | O_WRONLY,
                                S_IREAD | S_IWRITE | S_IFREG);

        if (handle == -1)
                return false;

        if (!write (handle,ptr,length))
        {
                close (handle);
                return false;
        }
        close (handle);
        return true;
}



/*
==========================
=
= CA_LoadFile
=
= Allocate space for and load a file
=
==========================
*/

boolean CA_LoadFile (char *filename, memptr *ptr)
{
        int handle;
        long size;

        if ((handle = open(filename,O_RDONLY | O_BINARY, S_IREAD)) == -1)
                return false;

        size = filelength (handle);
        *ptr=malloc(size);
        if (!read (handle,*ptr,size))
        {
                close (handle);
                return false;
        }
        close (handle);
        return true;
}

/*
============================================================================

                COMPRESSION routines, see JHUFF.C for more

============================================================================
*/

void CAL_HuffExpand(byte *source, byte *dest, long length,
        huffnode *hufftable, boolean screenhack)
{
        byte *end;//,*srcend;
        huffnode *headptr;
        byte mapmask;
        
        if(!length || !dest)
        {
                Quit("length or dest is null!");
                return;
        }

        headptr = hufftable+254;        // head node is allways node 254

        if(screenhack)
        {
                mapmask=1;
                VGAMAPMASK(1);
                length>>=2;
        }
        
        end=dest+length;

        //
        // ds:si source
        // es:di dest
        // ss:bx node pointer
        //

        _asm
        {
                xor     edx,edx
                mov     ebx,dword ptr [headptr]
                mov     esi,dword ptr [source]
                mov     edi,dword ptr [dest]
                mov     eax,dword ptr [end]
                mov     ch,[esi]                                // load first byte
                inc     esi
                mov     cl,1

expandshort:
                test    ch,cl                   // bit set?
                jnz     bit1short

                mov     dx,[ebx]                        // take bit0 path from node
                shl     cl,1                            // advance to next bit position
                jc      newbyteshort
                jnc     sourceupshort

bit1short:
                mov     dx,[ebx+2]              // take bit1 path
                shl     cl,1                            // advance to next bit position
                jnc     sourceupshort

newbyteshort:
                mov     ch,[esi]                                // load next byte
                inc esi

                mov     cl,1                            // back to first bit

sourceupshort:
                or      dh,dh                           // if dx<256 its a byte, else move node
                jz      storebyteshort

                mov     ebx,dword ptr [hufftable]                       // next node = hufftable+code-256
                lea     ebx,dword ptr [ebx+edx*4-1024]
                jmp     expandshort

storebyteshort:
                mov     [edi],dl
                inc     edi                                     // write a decompressed byte out
                mov     ebx,dword ptr [headptr]         // back to the head node for next bit

                cmp     edi,eax                         // done?

                jb expandshort
done:
                test    [screenhack],1
                jz              notscreen
                shl     [mapmask],1
                mov     ah,[mapmask]
                cmp     ah,16
                je              notscreen
                mov     dx,SC_INDEX
                mov     al,SC_MAPMASK
                out     dx,ax
                mov     edi,[dest]
                mov     eax,[end]
                jmp     expandshort
                
notscreen:
        }
}

/*
======================
=
= CAL_CarmackExpand
=
= Length is the length of the EXPANDED data
=
======================
*/

#define NEARTAG 0xa7
#define FARTAG  0xa8

void CAL_CarmackExpand (word *source, word *dest, int length)
{
        word ch,chhigh,count,offset;
        word *copyptr, *inptr, *outptr;

        length/=2;

        inptr = source;
        outptr = dest;

        while (length>0)
        {
                ch = *inptr++;
                chhigh = ch>>8;
                if (chhigh == NEARTAG)
                {
                        count = ch&0xff;
                        if (!count)
                        {                               // have to insert a word containing the tag byte
                                ch |= *((byte *)inptr);
                                inptr=(word *)(((byte *)inptr)+1);
                                *outptr++ = ch;
                                length--;
                        }
                        else
                        {
                                offset = *(byte *)inptr;
                                inptr=(word *)(((byte *)inptr)+1);
                                copyptr = outptr - offset;
                                length -= count;
                                if(length<0) return;
                                while (count--)
                                        *outptr++ = *copyptr++;
                        }
                }
                else if (chhigh == FARTAG)
                {
                        count = ch&0xff;
                        if (!count)
                        {                               // have to insert a word containing the tag byte
                                ch |= *((byte *)inptr);
                                inptr=(word *)(((byte *)inptr)+1);
                                *outptr++ = ch;
                                length --;
                        }
                        else
                        {
                                offset = *inptr++;
                                copyptr = dest + offset;
                                length -= count;
                                if(length<0) return;
                                while (count--)
                                        *outptr++ = *copyptr++;
                        }
                }
                else
                {
                        *outptr++ = ch;
                        length --;
                }
        }
}

/*
======================
=
= CA_RLEWcompress
=
======================
*/

long CA_RLEWCompress (word *source, long length, word *dest, word rlewtag)
{
  long complength;
  word value,count;
  unsigned i;
  word *start,*end;

  start = dest;

  end = source + (length+1)/2;

//
// compress it
//
  do
  {
        count = 1;
        value = *source++;
        while (*source == value && source<end)
        {
          count++;
          source++;
        }
        if (count>3 || value == rlewtag)
        {
    //
    // send a tag / count / value string
    //
      *dest++ = rlewtag;
      *dest++ = count;
      *dest++ = value;
    }
    else
    {
    //
    // send word without compressing
    //
      for (i=1;i<=count;i++)
                        *dest++ = value;
        }

  } while (source<end);

  complength = 2*(dest-start);
  return complength;
}


/*
======================
=
= CA_RLEWexpand
= length is EXPANDED length
=
======================
*/

void CA_RLEWexpand (word *source, word *dest, long length, word rlewtag)
{
        word value,count,i;
        word *end=dest+length/2;

//
// expand it
//
        do
        {
                value = *source++;
                if (value != rlewtag)
                        //
                        // uncompressed
                        //
                        *dest++=value;
                else
                {
                        //
                        // compressed string
                        //
                        count = *source++;
                        value = *source++;
                        for (i=1;i<=count;i++)
                                *dest++ = value;
                }
        } while (dest<end);
}



/*
=============================================================================

                                         CACHE MANAGER ROUTINES

=============================================================================
*/


/*
======================
=
= CAL_SetupGrFile
=
======================
*/

void CAL_SetupGrFile (void)
{
        char fname[13];
        int handle;
        byte *compseg;

#ifdef GRHEADERLINKED

        grhuffman = (huffnode *)&EGAdict;
        grstarts = (long _seg *)FP_SEG(&EGAhead);

#else

//
// load ???dict.ext (huffman dictionary for graphics files)
//

        strcpy(fname,gdictname);
        strcat(fname,extension);

        if ((handle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);

        read(handle, grhuffman, sizeof(grhuffman));
        close(handle);

//
// load the data offsets from ???head.ext
//
//      MM_GetPtr (&(memptr)grstarts,(NUMCHUNKS+1)*FILEPOSSIZE);

        strcpy(fname,gheadname);
        strcat(fname,extension);

        if ((handle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);

        read(handle, (memptr)grstarts, (NUMCHUNKS+1)*FILEPOSSIZE);

        close(handle);


#endif

//
// Open the graphics file, leaving it open until the game is finished
//
        strcpy(fname,gfilename);
        strcat(fname,extension);

        grhandle = open(fname, O_RDONLY | O_BINARY);
        if (grhandle == -1)
                CA_CannotOpen(fname);


//
// load the pic and sprite headers into the arrays in the data segment
//
        pictable=(pictabletype *) malloc(NUMPICS*sizeof(pictabletype));
        CAL_GetGrChunkLength(STRUCTPIC);                // position file pointer
        compseg=(byte *) malloc(chunkcomplen);
        read (grhandle,compseg,chunkcomplen);
        CAL_HuffExpand (compseg, (byte *)pictable,NUMPICS*sizeof(pictabletype),grhuffman,false);
        free(compseg);
}

//==========================================================================


/*
======================
=
= CAL_SetupMapFile
=
======================
*/

void CAL_SetupMapFile (void)
{
        int     i;
        int handle;
        long length,pos;
        char fname[13];

//
// load maphead.ext (offsets and tileinfo for map file)
//
        strcpy(fname,mheadname);
        strcat(fname,extension);

        if ((handle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);

        length = NUMMAPS*4+2; // used to be "filelength(handle);"
        mapfiletype *tinf=(mapfiletype *) malloc(sizeof(mapfiletype));
        read(handle, tinf, length);
        close(handle);

        RLEWtag=tinf->RLEWtag;
        
//
// open the data file
//
#ifdef CARMACIZED
        strcpy(fname,"GAMEMAPS.");
        strcat(fname,extension);

        if ((maphandle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);
#else
        strcpy(fname,mfilename);
        strcat(fname,extension);

        if ((maphandle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);
#endif

//
// load all map header
//
        for (i=0;i<NUMMAPS;i++)
        {
                pos = tinf->headeroffsets[i];
                if (pos<0)                                              // $FFFFFFFF start is a sparse map
                        continue;

                mapheaderseg[i]=(maptype *) malloc(sizeof(maptype));
                lseek(maphandle,pos,SEEK_SET);
                read (maphandle,(memptr)mapheaderseg[i],sizeof(maptype));
        }

        free(tinf);

//
// allocate space for 3 64*64 planes
//
        for (i=0;i<MAPPLANES;i++)
                mapsegs[i]=(word *) malloc(maparea*2);
}


//==========================================================================


/*
======================
=
= CAL_SetupAudioFile
=
======================
*/

void CAL_SetupAudioFile (void)
{
        int handle;
        long length;
        char fname[13];

//
// load audiohed.ext (offsets for audio file)
//
        strcpy(fname,aheadname);
        strcat(fname,extension);

        if ((handle = open(fname,
                 O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);

        length = filelength(handle);
        audiostarts=(long *) malloc(length);
        read(handle, audiostarts, length);
        close(handle);

//
// open the data file
//
        strcpy(fname,afilename);
        strcat(fname,extension);

        if ((audiohandle = open(fname, O_RDONLY | O_BINARY, S_IREAD)) == -1)
                CA_CannotOpen(fname);
}

//==========================================================================


/*
======================
=
= CA_Startup
=
= Open all files and load in headers
=
======================
*/

void CA_Startup (void)
{
#ifdef PROFILE
        unlink ("PROFILE.TXT");
        profilehandle = open("PROFILE.TXT", O_CREAT | O_WRONLY | O_TEXT);
#endif

        CAL_SetupMapFile ();
        CAL_SetupGrFile ();
        CAL_SetupAudioFile ();

        mapon = -1;
}

//==========================================================================


/*
======================
=
= CA_Shutdown
=
= Closes all files
=
======================
*/

void CA_Shutdown (void)
{
        int i,start;

        close (maphandle);
        close (grhandle);
        close (audiohandle);
        
        for(i=0;i<NUMCHUNKS;i++)
                if(grsegs[i])
                        UNCACHEGRCHUNK(i);
        free(pictable);
        
        switch (oldsoundmode)
        {
        case sdm_Off:
                return;
        case sdm_PC:
                start = STARTPCSOUNDS;
                break;
        case sdm_AdLib:
                start = STARTADLIBSOUNDS;
                break;
        }

        for (i=0;i<NUMSOUNDS;i++,start++)
                UNCACHEAUDIOCHUNK(start);
}

//===========================================================================

/*
======================
=
= CA_CacheAudioChunk
=
======================
*/

void CA_CacheAudioChunk (int chunk)
{
        long    pos,compressed;

        if (audiosegs[chunk])
                return;                                                 // already in memory

//
// load the chunk into a buffer, either the miscbuffer if it fits, or allocate
// a larger buffer
//
        pos = audiostarts[chunk];
        compressed = audiostarts[chunk+1]-pos;

        lseek(audiohandle,pos,SEEK_SET);

        audiosegs[chunk]=(byte *) malloc(compressed);
        if (!audiosegs[chunk])
                return;

        read(audiohandle,audiosegs[chunk],compressed);
}

//===========================================================================

/*
======================
=
= CA_LoadAllSounds
=
= Purges all sounds, then loads all new ones (mode switch)
=
======================
*/

void CA_LoadAllSounds (void)
{
        unsigned start,i;

        switch (oldsoundmode)
        {
        case sdm_Off:
                goto cachein;
        case sdm_PC:
                start = STARTPCSOUNDS;
                break;
        case sdm_AdLib:
                start = STARTADLIBSOUNDS;
                break;
        }

        for (i=0;i<NUMSOUNDS;i++,start++)
                UNCACHEAUDIOCHUNK(start);

cachein:

        oldsoundmode = SoundMode;

        switch (SoundMode)
        {
        case sdm_Off:
                return;
        case sdm_PC:
                start = STARTPCSOUNDS;
                break;
        case sdm_AdLib:
                start = STARTADLIBSOUNDS;
                break;
        }

        for (i=0;i<NUMSOUNDS;i++,start++)
                CA_CacheAudioChunk (start);
}

//===========================================================================


/*
======================
=
= CAL_ExpandGrChunk
=
= Does whatever is needed with a pointer to a compressed chunk
=
======================
*/

void CAL_ExpandGrChunk (int chunk, byte *source)
{
        long    expanded;

        if (chunk >= STARTTILE8 && chunk < STARTEXTERNS)
        {
        //
        // expanded sizes of tile8/16/32 are implicit
        //

#define BLOCK           64
#define MASKBLOCK       128

                if (chunk<STARTTILE8M)                  // tile 8s are all in one chunk!
                        expanded = BLOCK*NUMTILE8;
                else if (chunk<STARTTILE16)
                        expanded = MASKBLOCK*NUMTILE8M;
                else if (chunk<STARTTILE16M)    // all other tiles are one/chunk
                        expanded = BLOCK*4;
                else if (chunk<STARTTILE32)
                        expanded = MASKBLOCK*4;
                else if (chunk<STARTTILE32M)
                        expanded = BLOCK*16;
                else
                        expanded = MASKBLOCK*16;
        }
        else
        {
        //
        // everything else has an explicit size longword
        //
                expanded = *(long *) source;
                source += 4;                    // skip over length
        }

//
// allocate final space, decompress it, and free bigbuffer
// Sprites need to have shifts made and various other junk
//
        grsegs[chunk]=(byte *) malloc(expanded);
        if (!grsegs[chunk])
                return;
        CAL_HuffExpand (source,grsegs[chunk],expanded,grhuffman,false);
}


/*
======================
=
= CA_CacheGrChunk
=
= Makes sure a given chunk is in memory, loadiing it if needed
=
======================
*/

void CA_CacheGrChunk (int chunk)
{
        long    pos,compressed;
        byte *bigbufferseg;
        byte    *source;
        int             next;

        if (grsegs[chunk])
                return;                                                 // already in memory

//
// load the chunk into a buffer, either the miscbuffer if it fits, or allocate
// a larger buffer
//
        pos = GRFILEPOS(chunk);
        if (pos<0)                                                      // $FFFFFFFF start is a sparse tile
          return;

        next = chunk +1;
        while (GRFILEPOS(next) == -1)           // skip past any sparse tiles
                next++;

        compressed = GRFILEPOS(next)-pos;

        lseek(grhandle,pos,SEEK_SET);

        if (compressed<=BUFFERSIZE)
        {
                read(grhandle,bufferseg,compressed);
                source = bufferseg;
        }
        else
        {
                bigbufferseg=(byte *) malloc(compressed);
                read(grhandle,bigbufferseg,compressed);
                source = bigbufferseg;
        }

        CAL_ExpandGrChunk (chunk,source);

        if (compressed>BUFFERSIZE)
                free(bigbufferseg);
}



//==========================================================================

/*
======================
=
= CA_CacheScreen
=
= Decompresses a chunk from disk straight onto the screen
=
======================
*/

void CA_CacheScreen (int chunk)
{
        long    pos,compressed,expanded;
        memptr  bigbufferseg;
        byte    *source;
        int             next;

//
// load the chunk into a buffer
//
        pos = GRFILEPOS(chunk);
        next = chunk +1;
        while (GRFILEPOS(next) == -1)           // skip past any sparse tiles
                next++;
        compressed = GRFILEPOS(next)-pos;

        lseek(grhandle,pos,SEEK_SET);

        bigbufferseg=malloc(compressed);
        read(grhandle,bigbufferseg,compressed);
        source = (byte *) bigbufferseg;

        expanded = *(long *) source;
        source += 4;                    // skip over length

//
// allocate final space, decompress it, and free bigbuffer
// Sprites need to have shifts made and various other junk
//
        CAL_HuffExpand (source,vbuf,expanded,grhuffman,true);
        VW_MarkUpdateBlock (0,0,319,199);
        free(bigbufferseg);
}

//==========================================================================

/*
======================
=
= CA_CacheMap
=
= WOLF: This is specialized for a 64*64 map size
=
======================
*/

void CA_CacheMap (int mapnum)
{
        long    pos,compressed;
        int             plane;
        memptr  *dest;
        memptr bigbufferseg;
        unsigned size;
        word *source;
#ifdef CARMACIZED
        memptr  buffer2seg;
        long    expanded;
#endif

        mapon = mapnum;

//
// load the planes into the allready allocated buffers
//
        size = maparea*2;

        for (plane = 0; plane<MAPPLANES; plane++)
        {
                pos = mapheaderseg[mapnum]->planestart[plane];
                compressed = mapheaderseg[mapnum]->planelength[plane];

                dest = (memptr *) &mapsegs[plane];

                lseek(maphandle,pos,SEEK_SET);
                if (compressed<=BUFFERSIZE)
                        source = (word *) bufferseg;
                else
                {
                        bigbufferseg=malloc(compressed);
                        source = (word *) bigbufferseg;
                }

                read(maphandle,source,compressed);
#ifdef CARMACIZED
                //
                // unhuffman, then unRLEW
                // The huffman'd chunk has a two byte expanded length first
                // The resulting RLEW chunk also does, even though it's not really
                // needed
                //
                expanded = *source;
                source++;
                buffer2seg=malloc(expanded);
                CAL_CarmackExpand (source, (word *)buffer2seg,expanded);
                CA_RLEWexpand (((word *)buffer2seg)+1,(word *) *dest,size,RLEWtag);
                free(buffer2seg);

#else
                //
                // unRLEW, skipping expanded length
                //
                CA_RLEWexpand (source+1, *dest,size,RLEWtag);
#endif

                if (compressed>BUFFERSIZE)
                        free(bigbufferseg);
        }
}

//===========================================================================

void CA_CannotOpen(char *string)
{
        char str[30];

        strcpy(str,"Can't open ");
        strcat(str,string);
        strcat(str,"!\n");
        Quit (str);
}
