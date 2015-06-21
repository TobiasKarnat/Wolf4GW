// WL_DRAW.C

#include <DOS.H>

//#define DEBUGWALLS

/*
=============================================================================

                                                 LOCAL CONSTANTS

=============================================================================
*/

// the door is the last picture before the sprites
#define DOORWALL        (PMSpriteStart-8)

#define ACTORSIZE       0x4000

/*
=============================================================================

                                                 GLOBAL VARIABLES

=============================================================================
*/

#ifdef DEBUGWALLS
unsigned screenloc[3]= {0,0,0};
#else
unsigned screenloc[3]= {PAGE1START,PAGE2START,PAGE3START};
#endif
unsigned freelatch = FREESTART;

long    lasttimecount;
long    frameon;
boolean fpscounter;

int         fps_frames=0, fps_time=0, fps=0;

int wallheight[MAXVIEWWIDTH];

//
// math tables
//
short pixelangle[MAXVIEWWIDTH];
long finetangent[FINEANGLES/4];
fixed sintable[ANGLES+ANGLES/4];
fixed *costable = sintable+(ANGLES/4);

//
// refresh variables
//
fixed   viewx,viewy;                    // the focal point
short viewangle;
fixed   viewsin,viewcos;

void    TransformActor (objtype *ob);
void    BuildTables (void);
void    ClearScreen (void);
int     CalcRotate (objtype *ob);
void    DrawScaleds (void);
void    CalcTics (void);
void    FixOfs (void);
void    ThreeDRefresh (void);



//
// wall optimization variables
//
int     lastside;               // true for vertical
long    lastintercept;
int     lasttilehit;
int     lasttexture;

//
// ray tracing variables
//
short focaltx,focalty,viewtx,viewty;
longword xpartialup,xpartialdown,ypartialup,ypartialdown;

short midangle;

word tilehit;
int pixx;

short xtile,ytile;
short xtilestep,ytilestep;
long xintercept,yintercept;
word xspot,yspot;
int texdelta;

word horizwall[MAXWALLTILES],vertwall[MAXWALLTILES];


/*
============================================================================

                           3 - D  DEFINITIONS

============================================================================
*/

/*
========================
=
= TransformActor
=
= Takes paramaters:
=   gx,gy               : globalx/globaly of point
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
========================
*/


//
// transform actor
//
void TransformActor (objtype *ob)
{
        fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
        gx = ob->x-viewx;
        gy = ob->y-viewy;

//
// calculate newx
//
        gxt = FixedMul(gx,viewcos);
        gyt = FixedMul(gy,viewsin);
        nx = gxt-gyt-ACTORSIZE;         // fudge the shape forward a bit, because
                                                                // the midpoint could put parts of the shape
                                                                // into an adjacent wall

//
// calculate newy
//
        gxt = FixedMul(gx,viewsin);
        gyt = FixedMul(gy,viewcos);
        ny = gyt+gxt;

//
// calculate perspective ratio
//
        ob->transx = nx;
        ob->transy = ny;

        if (nx<mindist)                 // too close, don't overflow the divide
        {
          ob->viewheight = 0;
          return;
        }

        ob->viewx = (word)(centerx + ny*scale/nx);      // DEBUG: use assembly divide

//
// calculate height (heightnumerator/(nx>>8))
//
        ob->viewheight = (word)(heightnumerator/(nx>>8));
}

//==========================================================================

/*
========================
=
= TransformTile
=
= Takes paramaters:
=   tx,ty               : tile the object is centered in
=
= globals:
=   viewx,viewy         : point of view
=   viewcos,viewsin     : sin/cos of viewangle
=   scale               : conversion from global value to screen value
=
= sets:
=   screenx,transx,transy,screenheight: projected edge location and size
=
= Returns true if the tile is withing getting distance
=
========================
*/

boolean TransformTile (int tx, int ty, short *dispx, short *dispheight)
{
        fixed gx,gy,gxt,gyt,nx,ny;

//
// translate point to view centered coordinates
//
        gx = ((long)tx<<TILESHIFT)+0x8000-viewx;
        gy = ((long)ty<<TILESHIFT)+0x8000-viewy;

//
// calculate newx
//
        gxt = FixedMul(gx,viewcos);
        gyt = FixedMul(gy,viewsin);
        nx = gxt-gyt-0x2000;            // 0x2000 is size of object

//
// calculate newy
//
        gxt = FixedMul(gx,viewsin);
        gyt = FixedMul(gy,viewcos);
        ny = gyt+gxt;


//
// calculate height / perspective ratio
//
        if (nx<mindist)                 // too close, don't overflow the divide
                *dispheight = 0;
        else
        {
                *dispx = (short)(centerx + ny*scale/nx);        // DEBUG: use assembly divide
                *dispheight = (short)(heightnumerator/(nx>>8));
        }

//
// see if it should be grabbed
//
        if (nx<TILEGLOBAL && ny>-TILEGLOBAL/2 && ny<TILEGLOBAL/2)
                return true;
        else
                return false;
}

//==========================================================================

/*
====================
=
= CalcHeight
=
= Calculates the height of xintercept,yintercept from viewx,viewy
=
====================
*/

int CalcHeight();
#pragma aux CalcHeight = \
        "mov    eax,dword ptr xintercept" \
        "sub    eax,dword ptr viewx" \
        "imul   dword ptr viewcos" \
        "shrd   eax,edx,16" \
        "mov    edi,eax" \
        "mov    eax,dword ptr yintercept" \
        "sub    eax,dword ptr viewy" \
        "imul   dword ptr viewsin" \
        "shrd   eax,edx,16" \
        "sub    edi,eax" \
        "cmp    edi,1024" \
        "jge    dontclip" \
        "mov    edi,1024" \
        "dontclip:" \
        "xor    edx,edx" \
        "sar    edi,9" \
        "mov    eax,dword ptr heightnumerator" \
        "sar    eax,1" \
        "div    edi" \
        value [eax] \
        modify exact [eax edx edi]

//==========================================================================

/*
===================
=
= ScalePost
=
===================
*/

byte *postsource;
int postx;
int postwidth;

// usage: masks[width-1][xpos&3]
byte masks[4][4]={{1,2,4,8},{3,6,12,0},{7,14,0,0},{15,0,0,0}};

void ScalePost()
{
   int ywcount,yoffs,yw,yd,yendoffs;
   byte col;

   VGAMAPMASK(masks[postwidth-1][postx&3]);
   ywcount=yd=wallheight[postx]>>3;

   if(yd<=0) yd=100;
   yoffs=(viewheight/2-ywcount)*80;
   if(yoffs<0) yoffs=0;
   yoffs+=postx>>2;
   yendoffs=viewheight/2+ywcount-1;
   yw=63;
   while(yendoffs>=viewheight)
   {
      ywcount-=32;
      while(ywcount<=0)
      {
         ywcount+=yd;
         yw--;
      }
      yendoffs--;
   }
   if(yw<0) return;
   col=((byte *)postsource)[yw];
   yendoffs=yendoffs*80+(postx>>2);
   while(yoffs<=yendoffs)
   {
      vbuf[yendoffs]=col;
      ywcount-=32;
      if(ywcount<=0)
      {
         do
         {
            ywcount+=yd;
            yw--;
         }
         while(ywcount<=0);
         if(yw<0) break;
         col=postsource[yw];
      }
      yendoffs-=80;
   }
}


/*
====================
=
= HitVertWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitVertWall (void)
{
        int wallpic;
        int texture;

        texture = ((yintercept+texdelta)>>4)&0xfc0;
        if (xtilestep == -1)
        {
                texture = 0xfc0-texture;
                xintercept += TILEGLOBAL;
        }

        if(lastside==1 && lastintercept==xtile && lasttilehit==tilehit && !(lasttilehit & 0x40))
        {
                if((pixx&3) && texture == lasttexture)
                {
                        postwidth++;
                        wallheight[pixx] = wallheight[pixx-1];
                        return;
                }
                ScalePost();
                wallheight[pixx] = CalcHeight();
                postsource+=texture-lasttexture;
                postwidth=1;
                postx=pixx;
                lasttexture=texture;
                return;
        }

        if(lastside!=-1) ScalePost();

        lastside=1;
        lastintercept=xtile;
        lasttilehit=tilehit;
        lasttexture=texture;
        wallheight[pixx] = CalcHeight();
        postx = pixx;
        postwidth = 1;

        if (tilehit & 0x40)
        {                                                               // check for adjacent doors
                ytile = (short)(yintercept>>TILESHIFT);
                if ( tilemap[xtile-xtilestep][ytile]&0x80 )
                        wallpic = DOORWALL+3;
                else
                        wallpic = vertwall[tilehit & ~0x40];
        }
        else
                wallpic = vertwall[tilehit];

        postsource = Pages+(wallpic<<12)+texture;
}


/*
====================
=
= HitHorizWall
=
= tilehit bit 7 is 0, because it's not a door tile
= if bit 6 is 1 and the adjacent tile is a door tile, use door side pic
=
====================
*/

void HitHorizWall (void)
{
        int wallpic;
        int texture;

        texture = ((xintercept+texdelta)>>4)&0xfc0;
        if (ytilestep == -1)
                yintercept += TILEGLOBAL;
        else
                texture = 0xfc0-texture;

        if(lastside==0 && lastintercept==ytile && lasttilehit==tilehit && !(lasttilehit & 0x40))
        {
                if((pixx&3) && texture == lasttexture)
                {
                        postwidth++;
                        wallheight[pixx] = wallheight[pixx-1];
                        return;
                }
                ScalePost();
                wallheight[pixx] = CalcHeight();
                postsource+=texture-lasttexture;
                postwidth=1;
                postx=pixx;
                lasttexture=texture;
                return;
        }

        if(lastside!=-1) ScalePost();

        lastside=0;
        lastintercept=ytile;
        lasttilehit=tilehit;
        lasttexture=texture;
        wallheight[pixx] = CalcHeight();
        postx = pixx;
        postwidth = 1;

        if (tilehit & 0x40)
        {                                                               // check for adjacent doors
                xtile = (short)(xintercept>>TILESHIFT);
                if ( tilemap[xtile][ytile-ytilestep]&0x80)
                        wallpic = DOORWALL+2;
                else
                        wallpic = horizwall[tilehit & ~0x40];
        }
        else
                wallpic = horizwall[tilehit];

        postsource = Pages+(wallpic<<12)+texture;
}

//==========================================================================

/*
====================
=
= HitHorizDoor
=
====================
*/

void HitHorizDoor (void)
{
        int doorpage;
        int doornum;
        int texture;

        doornum = tilehit&0x7f;
        texture = ((xintercept-doorposition[doornum])>>4)&0xfc0;

        if(lasttilehit==tilehit)
        {
                if((pixx&3) && texture == lasttexture)
                {
                        postwidth++;
                        wallheight[pixx] = wallheight[pixx-1];
                        return;
                }
                ScalePost();
                wallheight[pixx] = CalcHeight();
                postsource+=texture-lasttexture;
                postwidth=1;
                postx=pixx;
                lasttexture=texture;
                return;
        }

        if(lastside!=-1) ScalePost();

        lastside=2;
        lasttilehit=tilehit;
        lasttexture=texture;
        wallheight[pixx] = CalcHeight();
        postx = pixx;
        postwidth = 1;

        switch(doorobjlist[doornum].lock)
        {
                case dr_normal:
                        doorpage = DOORWALL;
                        break;
                case dr_lock1:
                case dr_lock2:
                case dr_lock3:
                case dr_lock4:
                        doorpage = DOORWALL+6;
                        break;
                case dr_elevator:
                        doorpage = DOORWALL+4;
                        break;
        }

        postsource = Pages+(doorpage<<12)+texture;
}

//==========================================================================

/*
====================
=
= HitVertDoor
=
====================
*/

void HitVertDoor (void)
{
        int doorpage;
        int doornum;
        int texture;

        doornum = tilehit&0x7f;
        texture = ((yintercept-doorposition[doornum])>>4)&0xfc0;

        if(lasttilehit==tilehit)
        {
                if((pixx&3) && texture == lasttexture)
                {
                        postwidth++;
                        wallheight[pixx] = wallheight[pixx-1];
                        return;
                }
                ScalePost();
                wallheight[pixx] = CalcHeight();
                postsource+=texture-lasttexture;
                postwidth=1;
                postx=pixx;
                lasttexture=texture;
                return;
        }

        if(lastside!=-1) ScalePost();

        lastside=2;
        lasttilehit=tilehit;
        lasttexture=texture;
        wallheight[pixx] = CalcHeight();
        postx = pixx;
        postwidth = 1;

        switch(doorobjlist[doornum].lock)
        {
                case dr_normal:
                        doorpage = DOORWALL+1;
                        break;
                case dr_lock1:
                case dr_lock2:
                case dr_lock3:
                case dr_lock4:
                        doorpage = DOORWALL+7;
                        break;
                case dr_elevator:
                        doorpage = DOORWALL+5;
                        break;
        }

        postsource = Pages+(doorpage<<12)+texture;
}

//==========================================================================

#define HitHorizBorder HitHorizWall
#define HitVertBorder HitVertWall

//==========================================================================

unsigned vgaCeiling[]=
{
#ifndef SPEAR
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xbfbf,
 0x4e4e,0x4e4e,0x4e4e,0x1d1d,0x8d8d,0x4e4e,0x1d1d,0x2d2d,0x1d1d,0x8d8d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x1d1d,0x2d2d,0xdddd,0x1d1d,0x1d1d,0x9898,

 0x1d1d,0x9d9d,0x2d2d,0xdddd,0xdddd,0x9d9d,0x2d2d,0x4d4d,0x1d1d,0xdddd,
 0x7d7d,0x1d1d,0x2d2d,0x2d2d,0xdddd,0xd7d7,0x1d1d,0x1d1d,0x1d1d,0x2d2d,
 0x1d1d,0x1d1d,0x1d1d,0x1d1d,0xdddd,0xdddd,0x7d7d,0xdddd,0xdddd,0xdddd
#else
 0x6f6f,0x4f4f,0x1d1d,0xdede,0xdfdf,0x2e2e,0x7f7f,0x9e9e,0xaeae,0x7f7f,
 0x1d1d,0xdede,0xdfdf,0xdede,0xdfdf,0xdede,0xe1e1,0xdcdc,0x2e2e,0x1d1d,0xdcdc
#endif
};

/*
=====================
=
= VGAClearScreen
=
=====================
*/

void VGAClearScreen (void)
{
        unsigned ceiling=vgaCeiling[gamestate.episode*10+mapon];
        ceiling|=ceiling<<16;

        _asm {
                cli
                mov     edx,SC_INDEX
                mov     eax,SC_MAPMASK+15*256
                out     dx,ax
                sti
                mov     edx,80
                mov     ebx,[viewwidth]
                shr     ebx,2
                sub     edx,ebx

                shr     ebx,2
                mov     bh,byte ptr [viewheight]
                shr     bh,1

                mov     edi,[vbuf]
                mov     eax,[ceiling]
                xor     ecx,ecx

toploop:
                mov     cl,bl
                rep     stosd
                add     edi,edx
                dec     bh
                jnz     toploop

                mov     bh,byte ptr [viewheight]
                shr     bh,1
                mov     eax,0x19191919

bottomloop:
                mov     cl,bl
                rep     stosd
                add     edi,edx
                dec     bh
                jnz     bottomloop
        }
}

//==========================================================================

/*
=====================
=
= CalcRotate
=
=====================
*/

int     CalcRotate (objtype *ob)
{
        int     angle,viewangle;

        // this isn't exactly correct, as it should vary by a trig value,
        // but it is close enough with only eight rotations

        viewangle = player->angle + (centerx - ob->viewx)/8;

        if (ob->obclass == rocketobj || ob->obclass == hrocketobj)
                angle =  (viewangle-180)- ob->angle;
        else
                angle =  (viewangle-180)- dirangle[ob->dir];

        angle+=ANGLES/16;
        while (angle>=ANGLES)
                angle-=ANGLES;
        while (angle<0)
                angle+=ANGLES;

        if (ob->state->rotate == 2)             // 2 rotation pain frame
                return 0;               // pain with shooting frame bugfix

        return angle/(ANGLES/8);
}


void ScaleShape (int xcenter, int shapenum, unsigned height)
{
        t_compshape   *shape;
        unsigned scale,pixheight;
        unsigned starty,endy;
        word *cmdptr;
        word *cline;
        word *line;
        byte *vmem;
        int actx,i,upperedge;
        short newstart;
        int scrstarty,screndy,j,lpix,rpix,pixcnt,ycnt;
        byte col;
#ifdef SHADE_COUNT
        byte shade;
        byte *curshades;
#endif

        shape=(t_compshape *)(Pages+((PMSpriteStart+shapenum)<<12));

        scale=height>>3;                 // low three bits are fractional
        if(!scale/* || scale>maxscale*/) return;   // too close or far away

#ifdef SHADE_COUNT
        switch(shapenum)
        {
                case 5:
                case 6:
                case 16:
                case 437:
                        shade=0;   // let lamps "shine" in the dark
                        break;
                default:
                        shade=(scale<<2)/((maxscaleshl2>>8)+1+LSHADE_flag);
                        if(shade>32) shade=32;
                        else if(shade<1) shade=1;
                        shade=32-shade;
                        break;
        }
        curshades=shadetable[shade];
#endif

        pixheight=scale*2;
        actx=xcenter-scale;
        upperedge=viewheight/2-scale;

        cmdptr=(word *) shape->dataofs;

        for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
        {
                lpix=rpix;
                if(lpix>=viewwidth) break;
                pixcnt+=pixheight;
                rpix=(pixcnt>>6)+actx;
                if(lpix!=rpix && rpix>0)
                {
                        if(lpix<0) lpix=0;
                        if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
                        cline=(word *)((byte *)shape + *cmdptr);
                        while(lpix<rpix)
                        {
                                if(wallheight[lpix]<=height)
                                {
                                        VGAMAPMASK(1<<(lpix&3));
                                        line=cline;
                                        while(*line)
                                        {
                                                starty=(*(line+2))>>1;
                                                endy=(*line)>>1;
                                                newstart=(*(line+1));
                                                j=starty;
                                                ycnt=j*pixheight;
                                                screndy=(ycnt>>6)+upperedge;
                                                if(screndy<0) vmem=vbuf+(lpix>>2);
                                                else vmem=vbuf+screndy*80+(lpix>>2);
                                                for(;j<endy;j++)
                                                {
                                                        scrstarty=screndy;
                                                        ycnt+=pixheight;
                                                        screndy=(ycnt>>6)+upperedge;
                                                        if(scrstarty!=screndy && screndy>0)
                                                        {
#ifdef SHADE_COUNT
                                                                col=curshades[((byte _seg *)shape)[newstart+j]];
#else
                                                                col=((byte *)shape)[newstart+j];
#endif
                                                                if(scrstarty<0) scrstarty=0;
                                                                if(screndy>viewheight) screndy=viewheight,j=endy;

                                                                while(scrstarty<screndy)
                                                                {
                                                                        *vmem=col;
                                                                        vmem+=80;
                                                                        scrstarty++;
                                                                }
                                                        }
                                                }
                                                line+=3;
                                        }
                                }
                                lpix++;
                        }
                }
        }
}

void SimpleScaleShape (int xcenter, int shapenum, unsigned height)
{
        t_compshape   *shape;
        unsigned scale,pixheight;
        unsigned starty,endy;
        word *cmdptr;
        word *cline;
        word *line;
        int actx,i,upperedge;
        short newstart;
        int scrstarty,screndy,j,lpix,rpix,pixcnt,ycnt;
        byte mask,col;
        byte *vmem;

        shape=(t_compshape *)(Pages+((PMSpriteStart+shapenum)<<12));

        scale=height>>1;                 // low three bits are fractional
        if(!scale/* || scale>maxscale*/) return;   // too close or far away

        pixheight=scale*2;
        actx=xcenter-scale;
        upperedge=viewheight/2-scale;

        cmdptr=shape->dataofs;

        for(i=shape->leftpix,pixcnt=i*pixheight,rpix=(pixcnt>>6)+actx;i<=shape->rightpix;i++,cmdptr++)
        {
                lpix=rpix;
                if(lpix>=viewwidth) break;
                pixcnt+=pixheight;
                rpix=(pixcnt>>6)+actx;
                if(lpix!=rpix && rpix>0)
                {
                        if(lpix<0) lpix=0;
                        if(rpix>viewwidth) rpix=viewwidth,i=shape->rightpix+1;
                        cline=(word *)((byte *)shape + *cmdptr);
                        while(lpix<rpix)
                        {
                                mask=1<<(lpix&3);
                                VGAMAPMASK(mask);
                                line=cline;
                                while(*line)
                                {
                                        starty=(*(line+2))>>1;
                                        endy=(*line)>>1;
                                        newstart=(*(line+1));
                                        j=starty;
                                        ycnt=j*pixheight;
                                        screndy=(ycnt>>6)+upperedge;
                                        if(screndy<0) vmem=vbuf+(lpix>>2);
                                        else vmem=vbuf+screndy*80+(lpix>>2);
                                        for(;j<endy;j++)
                                        {
                                                scrstarty=screndy;
                                                ycnt+=pixheight;
                                                screndy=(ycnt>>6)+upperedge;
                                                if(scrstarty!=screndy && screndy>0)
                                                {
                                                        col=((byte *)shape)[newstart+j];
                                                        if(scrstarty<0) scrstarty=0;
                                                        if(screndy>viewheight) screndy=viewheight,j=endy;

                                                        while(scrstarty<screndy)
                                                        {
                                                                *vmem=col;
                                                                vmem+=80;
                                                                scrstarty++;
                                                        }
                                                }
                                        }
                                        line+=3;
                                }
                                lpix++;
                        }
                }
        }
}

/*
=====================
=
= DrawScaleds
=
= Draws all objects that are visable
=
=====================
*/

#define MAXVISABLE      50

typedef struct
{
        short   viewx,
                viewheight,
                shapenum;
} visobj_t;

visobj_t        vislist[MAXVISABLE];
visobj_t *visptr,*visstep,*farthest;

void DrawScaleds (void)
{
        int             i,least,numvisable,height;
        byte            *tilespot,*visspot;
        unsigned spotloc;

        statobj_t       *statptr;
        objtype         *obj;

        visptr = &vislist[0];

//
// place static objects
//
        for (statptr = &statobjlist[0] ; statptr !=laststatobj ; statptr++)
        {
                if ((visptr->shapenum = statptr->shapenum) == -1)
                        continue;                                               // object has been deleted

                if (!*statptr->visspot)
                        continue;                                               // not visable

                if (TransformTile (statptr->tilex,statptr->tiley
                        ,&visptr->viewx,&visptr->viewheight) && statptr->flags & FL_BONUS)
                {
                        GetBonus (statptr);
                        continue;
                }

                if (!visptr->viewheight)
                        continue;                                               // to close to the object

                if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
                        visptr++;
        }

//
// place active objects
//
        for (obj = player->next;obj;obj=obj->next)
        {
                if ((visptr->shapenum = obj->state->shapenum)==0)
                        continue;                                               // no shape

                spotloc = (obj->tilex<<mapshift)+obj->tiley;   // optimize: keep in struct?
                visspot = &spotvis[0][0]+spotloc;
                tilespot = &tilemap[0][0]+spotloc;

                //
                // could be in any of the nine surrounding tiles
                //
                if (*visspot
                || ( *(visspot-1) && !*(tilespot-1) )
                || ( *(visspot+1) && !*(tilespot+1) )
                || ( *(visspot-65) && !*(tilespot-65) )
                || ( *(visspot-64) && !*(tilespot-64) )
                || ( *(visspot-63) && !*(tilespot-63) )
                || ( *(visspot+65) && !*(tilespot+65) )
                || ( *(visspot+64) && !*(tilespot+64) )
                || ( *(visspot+63) && !*(tilespot+63) ) )
                {
                        obj->active = ac_yes;
                        TransformActor (obj);
                        if (!obj->viewheight)
                                continue;                                               // too close or far away

                        visptr->viewx = obj->viewx;
                        visptr->viewheight = obj->viewheight;
                        if (visptr->shapenum == -1)
                                visptr->shapenum = obj->temp1;  // special shape

                        if (obj->state->rotate)
                                visptr->shapenum += CalcRotate (obj);

                        if (visptr < &vislist[MAXVISABLE-1])    // don't let it overflow
                                visptr++;
                        obj->flags |= FL_VISABLE;
                }
                else
                        obj->flags &= ~FL_VISABLE;
        }

//
// draw from back to front
//
        numvisable = visptr-&vislist[0];

        if (!numvisable)
                return;                                                                 // no visable objects

        for (i = 0; i<numvisable; i++)
        {
                least = 32000;
                for (visstep=&vislist[0] ; visstep<visptr ; visstep++)
                {
                        height = visstep->viewheight;
                        if (height < least)
                        {
                                least = height;
                                farthest = visstep;
                        }
                }
                //
                // draw farthest
                //
                ScaleShape(farthest->viewx,farthest->shapenum,farthest->viewheight);

                farthest->viewheight = 32000;
        }

}

//==========================================================================

/*
==============
=
= DrawPlayerWeapon
=
= Draw the player's hands
=
==============
*/

int     weaponscale[NUMWEAPONS] = {SPR_KNIFEREADY,SPR_PISTOLREADY
        ,SPR_MACHINEGUNREADY,SPR_CHAINREADY};

void DrawPlayerWeapon (void)
{
        int     shapenum;

#ifndef SPEAR
        if (gamestate.victoryflag)
        {
                if (player->state == &s_deathcam && (TimeCount&32) )
                        SimpleScaleShape(viewwidth/2,SPR_DEATHCAM,viewheight+1);
                return;
        }
#endif

        if (gamestate.weapon != -1)
        {
                shapenum = weaponscale[gamestate.weapon]+gamestate.weaponframe;
                SimpleScaleShape(viewwidth/2,shapenum,viewheight+1);
        }

        if (demorecord || demoplayback)
                SimpleScaleShape(viewwidth/2,SPR_DEMO,viewheight+1);
}


//==========================================================================


/*
=====================
=
= CalcTics
=
=====================
*/

void CalcTics (void)
{
        long    newtime;

//
// calculate tics since last refresh for adaptive timing
//
        if (lasttimecount > TimeCount)
                TimeCount = lasttimecount;              // if the game was paused a LONG time

        do
        {
                newtime = TimeCount;
                tics = newtime-lasttimecount;
        } while (!tics);                        // make sure at least one tic passes

        lasttimecount = newtime;

        if (tics>MAXTICS)
        {
                TimeCount -= (tics-MAXTICS);
                tics = MAXTICS;
        }
}


//==========================================================================


/*
========================
=
= FixOfs
=
========================
*/

void    FixOfs (void)
{
        VW_ScreenToScreen (vdisp,vbuf,viewwidth/8,viewheight);
}


//==========================================================================

//#define DEBUGRAYTRACER

#ifdef DEBUGRAYTRACER
#define LOGF if(dolog) fprintf
#define MARKPIX(y,col) VGAMAPMASK(1<<(pixx&3)); \
	vbuf[(pixx>>2)+(y)*80]=(col);
#else
#define LOGF 0 &&
#define MARKPIX(y,col)
#endif

void AsmRefresh()
{
    long xstep,ystep;
    longword xpartial,ypartial;

#ifdef DEBUGRAYTRACER
	 static int logpressed=0;
	 int dolog=0;
    FILE *log=NULL;
#endif

    for(pixx=0;pixx<viewwidth;pixx++)
    {
        short angl=midangle+pixelangle[pixx];
        if(angl<0) angl+=FINEANGLES;
        if(angl>=3600) angl-=FINEANGLES;
        if(angl<900)
        {
            xtilestep=1;
            ytilestep=-1;
            xstep=finetangent[900-1-angl];
            ystep=-finetangent[angl];
            xpartial=xpartialup;
            ypartial=ypartialdown;
        }
        else if(angl<1800)
        {
            xtilestep=-1;
            ytilestep=-1;
            xstep=-finetangent[angl-900];
            ystep=-finetangent[1800-1-angl];
            xpartial=xpartialdown;
            ypartial=ypartialdown;
        }
        else if(angl<2700)
        {
            xtilestep=-1;
            ytilestep=1;
            xstep=-finetangent[2700-1-angl];
            ystep=finetangent[angl-1800];
            xpartial=xpartialdown;
            ypartial=ypartialup;
        }
        else if(angl<3600)
        {
            xtilestep=1;
            ytilestep=1;
            xstep=finetangent[angl-2700];
            ystep=finetangent[3600-1-angl];
            xpartial=xpartialup;
            ypartial=ypartialup;
        }
        yintercept=FixedMul(ystep,xpartial)+viewy;
        xtile=focaltx+xtilestep;
        xspot=(xtile<<mapshift)+*((word *)&yintercept+1);
        xintercept=FixedMul(xstep,ypartial)+viewx;
        ytile=focalty+ytilestep;
        yspot=(*((word *)&xintercept+1)<<mapshift)+ytile;
		  texdelta=0;

        if(xintercept<0) xintercept=0;
        if(xintercept>mapwidth*65536-1) xintercept=mapwidth*65536-1;
        if(yintercept<0) yintercept=0;
        if(yintercept>mapheight*65536-1) yintercept=mapheight*65536-1;

#ifdef DEBUGRAYTRACER
		  if(pixx==93)
		  {
			  VGAMAPMASK(1<<(pixx&3));
			  vbuf[(pixx>>2)+80]=14;
			  if(logpressed)
			  {
				  if(!Keyboard[sc_L]) logpressed=0;
			  }
			  else
			  {
				  if(Keyboard[sc_L])
				  {
					  logpressed=1;
					  dolog=1;
					  log=fopen("draw93.txt","wt");
					  if(!log) return;
					  fprintf(log,"player->x=%.8X  player->y=%.8X  player->angle=%i  pixx=%i\nxintercept=%.8X  xtile=%.4X  xtilestep=%i  xstep=%.8X\nyintercept=%.8X  ytile=%.4X  ytilestep=%i  ystep=%.8X\n",player->x,player->y,player->angle,pixx,xintercept,xtile,xtilestep,xstep,yintercept,ytile,ytilestep,ystep);
				  }
			  }
		  }
#endif

        do
        {
            if(ytilestep==-1 && *((short *)&yintercept+1)<=ytile) goto horizentry;
            if(ytilestep==1 && *((short *)&yintercept+1)>=ytile) goto horizentry;
vertentry:
            if((longword)yintercept>mapheight*65536-1 || (word)xtile>=mapwidth)
            {
                if(xtile<0) xintercept=0;
                if(xtile>=mapwidth) xintercept=mapwidth<<TILESHIFT;
                if(yintercept<0) yintercept=0;
                if(yintercept>=(mapheight<<TILESHIFT)) yintercept=mapheight<<TILESHIFT;
                yspot=0xffff;
                HitHorizBorder();
                break;
            }
            if(xspot>mapspotend) break;
            tilehit=*((byte *)tilemap+xspot);
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    long yintbuf=yintercept+(ystep>>1);
                    if(*((word *)&yintbuf+1)!=*((word *)&yintercept+1))
                        goto passvert;
                    if((word)yintbuf<doorposition[tilehit&0x7f])
                        goto passvert;
                    yintercept=yintbuf;
                    xintercept=(xtile<<TILESHIFT)|0x8000;
                    HitVertDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
							  	if(pwalldir==di_west || pwalldir==di_east)
								{
	                        long yintbuf;
									int pwallposnorm;
									int pwallposinv;
									if(pwalldir==di_west)
									{
										pwallposnorm = 63-pwallpos;
										pwallposinv = pwallpos;
									}
									else
									{
										pwallposnorm = pwallpos;
										pwallposinv = 63-pwallpos;
									}
									if(pwalldir == di_east && xtile==pwallx && *((word *)&yintercept+1)==pwally
										|| pwalldir == di_west && !(xtile==pwallx && *((word *)&yintercept+1)==pwally))
									{
										yintbuf=yintercept+((ystep*pwallposnorm)>>6);
	   	                     if(*((word *)&yintbuf+1)!=*((word *)&yintercept+1))
   	   	                     goto passvert;
											
									   MARKPIX(4,2);

         		               xintercept=(xtile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
      	      	            yintercept=yintbuf;
										tilehit=pwalltile;
									   LOGF(log,"Pushwall hit 1: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%i\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	         HitVertWall();
									}
									else
									{
										yintbuf=yintercept+((ystep*pwallposinv)>>6);
	   	                     if(*((word *)&yintbuf+1)!=*((word *)&yintercept+1))
   	   	                     goto passvert;
										
									   MARKPIX(4,1);

         		               xintercept=(xtile<<TILESHIFT)-(pwallposinv<<10);
      	      	            yintercept=yintbuf;
										tilehit=pwalltile;
									   LOGF(log,"Pushwall hit 2: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%i\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	         HitVertWall();
									}
								}
								else
								{
									int pwallposi = pwallpos;
									if(pwalldir==di_north) pwallposi = 63-pwallpos;
									if(pwalldir==di_south && (word)yintercept<(pwallposi<<10)
											|| pwalldir==di_north && (word)yintercept>(pwallposi<<10))
									{
										if(*((word *)&yintercept+1)==pwally && xtile==pwallx)
										{												
										   if(pwalldir==di_south && (long)((word)yintercept)+ystep<(pwallposi<<10)
													|| pwalldir==di_north && (long)((word)yintercept)+ystep>(pwallposi<<10))
											   goto passvert;
												
										   MARKPIX(5,15);

									      LOGF(log,"Pushwall hit 3: HitHorizWall old values:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X",xintercept,xtile,yintercept,ytile,pwallpos);
											if(pwalldir==di_south)
											   yintercept=(yintercept&0xffff0000)+(pwallposi<<10);
											else
											   yintercept=(yintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
     		      	               xintercept=xintercept-((xstep*(63-pwallpos))>>6);
											tilehit=pwalltile;
									      LOGF(log,"Pushwall hit 3: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	            HitHorizWall();
										}
										else
										{
										   MARKPIX(3,11);
										
									      texdelta = -(pwallposi<<10);
											xintercept=xtile<<TILESHIFT;
											tilehit=pwalltile;
									      LOGF(log,"Pushwall hit 4: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
											HitVertWall();
										}
									}
									else
									{
										if(*((word *)&yintercept+1)==pwally && xtile==pwallx)
										{
										   MARKPIX(3,12);
										
									      texdelta = -(pwallposi<<10);
											xintercept=xtile<<TILESHIFT;
											tilehit=pwalltile;
										   LOGF(log,"Pushwall hit 5: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
											HitVertWall();
										}
										else
										{
										   if(pwalldir==di_south && (long)((word)yintercept)+ystep>(pwallposi<<10)
													|| pwalldir==di_north && (long)((word)yintercept)+ystep<(pwallposi<<10))
											   goto passvert;
											
										   MARKPIX(3,3);

											if(pwalldir==di_south)
											   yintercept=(yintercept&0xffff0000)-((63-pwallpos)<<10);
											else
											   yintercept=(yintercept&0xffff0000)+((63-pwallpos)<<10);
     		      	               xintercept=xintercept-((xstep*pwallpos)>>6);
											tilehit=pwalltile;
										   LOGF(log,"Pushwall hit 6: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	            HitHorizWall();
										}
									}
								}
                    }
                    else
                    {
                        xintercept=xtile<<TILESHIFT;
							   LOGF(log,"HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n",xintercept,xtile,yintercept,ytile);
                        HitVertWall();
                    }
                }
                break;
            }
passvert:
            *((byte *)spotvis+xspot)=1;
            xtile+=xtilestep;
            yintercept+=ystep;
            xspot=(xtile<<mapshift)+*((word *)&yintercept+1);
			   LOGF(log,"passvert:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n",xintercept,xtile,yintercept,ytile);
        }
        while(1);
#ifdef DEBUGRAYTRACER
		  if(dolog)
		  {
			  fclose(log);
			  dolog=0;
			  log=NULL;
		  }
#endif
        continue;
        do
        {
            if(xtilestep==-1 && *((short *)&xintercept+1)<=xtile) goto vertentry;
            if(xtilestep==1 && *((short *)&xintercept+1)>=xtile) goto vertentry;
horizentry:
            if((longword)xintercept>mapwidth*65536-1 || (word)ytile>=mapheight)
            {
                if(ytile<0) yintercept=0;
                if(ytile>=mapheight) yintercept=mapheight<<TILESHIFT;
                if(xintercept<0) xintercept=0;
                if(xintercept>=(mapwidth<<TILESHIFT)) xintercept=mapwidth<<TILESHIFT;
                xspot=0xffff;
                HitVertBorder();
                break;
            }
            if(yspot>mapspotend) break;
            tilehit=*((byte *)tilemap+yspot);
            if(tilehit)
            {
                if(tilehit&0x80)
                {
                    long xintbuf=xintercept+(xstep>>1);
                    if(*((word *)&xintbuf+1)!=*((word *)&xintercept+1))
                        goto passhoriz;
                    if((word)xintbuf<doorposition[tilehit&0x7f])
                        goto passhoriz;
                    xintercept=xintbuf;
                    yintercept=(ytile<<TILESHIFT)+0x8000;
                    HitHorizDoor();
                }
                else
                {
                    if(tilehit==64)
                    {
							   if(pwalldir==di_north || pwalldir==di_south)
								{
	                        long xintbuf;
									int pwallposnorm;
									int pwallposinv;
									if(pwalldir==di_north)
									{
										pwallposnorm = 63-pwallpos;
										pwallposinv = pwallpos;
									}
									else
									{
										pwallposnorm = pwallpos;
										pwallposinv = 63-pwallpos;
									}
									if(pwalldir == di_south && ytile==pwally && *((word *)&xintercept+1)==pwallx
										|| pwalldir == di_north && !(ytile==pwally && *((word *)&xintercept+1)==pwallx))
									{
										xintbuf=xintercept+((xstep*pwallposnorm)>>6);
	   	                     if(*((word *)&xintbuf+1)!=*((word *)&xintercept+1))
   	   	                     goto passhoriz;
											
									   MARKPIX(4,2);

         		               yintercept=(ytile<<TILESHIFT)+TILEGLOBAL-(pwallposinv<<10);
      	      	            xintercept=xintbuf;
										tilehit=pwalltile;
									   LOGF(log,"Pushwall hit 7: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%i\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	         HitHorizWall();
									}
									else
									{
										xintbuf=xintercept+((xstep*pwallposinv)>>6);
	   	                     if(*((word *)&xintbuf+1)!=*((word *)&xintercept+1))
   	   	                     goto passhoriz;
										
									   MARKPIX(4,1);

         		               yintercept=(ytile<<TILESHIFT)-(pwallposinv<<10);
      	      	            xintercept=xintbuf;
										tilehit=pwalltile;
									   LOGF(log,"Pushwall hit 8: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%i\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	         HitHorizWall();
									}
								}
								else
								{
									int pwallposi = pwallpos;
									if(pwalldir==di_west) pwallposi = 63-pwallpos;
									if(pwalldir==di_east && (word)xintercept<(pwallposi<<10)
											|| pwalldir==di_west && (word)xintercept>(pwallposi<<10))
									{
										if(*((word *)&xintercept+1)==pwallx && ytile==pwally)
										{												
										   if(pwalldir==di_east && (long)((word)xintercept)+xstep<(pwallposi<<10)
													|| pwalldir==di_west && (long)((word)xintercept)+xstep>(pwallposi<<10))
											   goto passhoriz;
												
										   MARKPIX(3,15);

									      LOGF(log,"Pushwall hit 9: HitVertWall old values:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X",xintercept,xtile,yintercept,ytile,pwallpos);

											if(pwalldir==di_east)
											   xintercept=(xintercept&0xffff0000)+(pwallposi<<10);
											else
											   xintercept=(xintercept&0xffff0000)-TILEGLOBAL+(pwallposi<<10);
     		      	               yintercept=yintercept-((ystep*(63-pwallpos))>>6);
											tilehit=pwalltile;
									      LOGF(log,"Pushwall hit 9: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	            HitVertWall();
										}
										else
										{
										   MARKPIX(3,11);
											
									      texdelta = -(pwallposi<<10);
											yintercept=ytile<<TILESHIFT;
											tilehit=pwalltile;
										   LOGF(log,"Pushwall hit 10: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
											HitHorizWall();
										}
									}
									else
									{
										if(*((word *)&xintercept+1)==pwallx && ytile==pwally)
										{
										   MARKPIX(3,12);
										
									      texdelta = -(pwallposi<<10);
											yintercept=ytile<<TILESHIFT;
											tilehit=pwalltile;
										   LOGF(log,"Pushwall hit 11: HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
											HitHorizWall();
										}
										else
										{
										   if(pwalldir==di_east && (long)((word)xintercept)+xstep>(pwallposi<<10)
													|| pwalldir==di_west && (long)((word)xintercept)+xstep<(pwallposi<<10))
											   goto passhoriz;
												
										   MARKPIX(3,3);

											if(pwalldir==di_east)
											   xintercept=(xintercept&0xffff0000)-((63-pwallpos)<<10);
											else
											   xintercept=(xintercept&0xffff0000)+((63-pwallpos)<<10);
     		      	               yintercept=yintercept-((ystep*pwallpos)>>6);
											tilehit=pwalltile;
										   LOGF(log,"Pushwall hit 12: HitVertWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n  pwallpos=%.4X\n",xintercept,xtile,yintercept,ytile,pwallpos);
	               	            HitVertWall();
										}
									}
								}
                    }
                    else
                    {
                        yintercept=ytile<<TILESHIFT;
							   LOGF(log,"HitHorizWall:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n",xintercept,xtile,yintercept,ytile);
                        HitHorizWall();
                    }
                }
                break;
            }
passhoriz:
            *((byte *)spotvis+yspot)=1;
            ytile+=ytilestep;
            xintercept+=xstep;
            yspot=(*((word *)&xintercept+1)<<mapshift)+ytile;
			   LOGF(log,"passhoriz:\n  xintercept=%.8X  xtile=%.4X\n  yintercept=%.8X  ytile=%.4X\n",xintercept,xtile,yintercept,ytile);
        }
        while(1);
#ifdef DEBUGRAYTRACER
		  if(dolog)
		  {
			  fclose(log);
			  dolog=0;
			  log=NULL;
		  }
#endif
    }
}

/*
====================
=
= WallRefresh
=
====================
*/

void WallRefresh (void)
{
//
// set up variables for this view
//
        viewangle = player->angle;
        midangle = viewangle*(FINEANGLES/ANGLES);
        viewsin = sintable[viewangle];
        viewcos = costable[viewangle];
        viewx = player->x - FixedMul(focallength,viewcos);
        viewy = player->y + FixedMul(focallength,viewsin);

        focaltx = (short)(viewx>>TILESHIFT);
        focalty = (short)(viewy>>TILESHIFT);

        viewtx = (short)(player->x >> TILESHIFT);
        viewty = (short)(player->y >> TILESHIFT);

        xpartialdown = viewx&(TILEGLOBAL-1);
        xpartialup = TILEGLOBAL-xpartialdown;
        ypartialdown = viewy&(TILEGLOBAL-1);
        ypartialup = TILEGLOBAL-ypartialdown;

        lastside = -1;                  // the first pixel is on a new wall
        AsmRefresh ();
        ScalePost ();                   // no more optimization on last post
}

//==========================================================================

/*
========================
=
= ThreeDRefresh
=
========================
*/

void    ThreeDRefresh (void)
{
//
// clear out the traced array
//
        memset(spotvis,0,maparea);
        spotvis[player->tilex][player->tiley] = 1;       // Detect all sprites over player fix

        vbuf+=screenofs;

//
// follow the walls from there to the right, drawwing as we go
//
        VGAClearScreen ();

        WallRefresh ();

//
// draw all the scaled images
//
        DrawScaleds();                  // draw scaled stuff
        DrawPlayerWeapon ();    // draw player's hands

//
// show screen and time last cycle
//

        if (fizzlein)
        {
                FizzleFade((long)vbuf-0xa0000,(long)vdisp-0xa0000+screenofs,viewwidth,viewheight,20,false);
                fizzlein = false;

                lasttimecount = TimeCount = 0;          // don't make a big tic count
        }
        
        vbuf-=screenofs;
        vdisp=vbuf;

        _asm {
                cli
                mov     edx,0x3d4
                mov     al,0x0c
                out     dx,al
                inc     edx
                mov     al,byte ptr vdisp+1
                out     dx,al
                sti
        }

        vbuf+=SCREENSIZE;

        if((long)vbuf > 0xa0000+PAGE3START)
                vbuf=(byte *) 0xa0000;

#ifndef REMDEBUG
        if (fpscounter)
        {
                fps_frames++;
                fps_time+=tics;

                if(fps_time>35)
                {
                        fps_time-=35;
                        fps=fps_frames<<1;
                        fps_frames=0;
                }
                fontnumber = 0;
                SETFONTCOLOR(7,127);
                PrintX=8; PrintY=190;
                VWB_Bar(2,189,50,10,bordercol);
                US_PrintSigned(fps);
                US_Print(" fps");
        }
#endif

                
}


//===========================================================================

