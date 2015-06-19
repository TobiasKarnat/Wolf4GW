//
//	ID Engine
//	ID_US.h - Header file for the User Manager
//	v1.0d1
//	By Jason Blochowiak
//

#ifndef	__ID_US__
#define	__ID_US__

#ifdef	__DEBUG__
#define	__DEBUG_UserMgr__
#endif

//#define	HELPTEXTLINKED

#define	MaxX	320
#define	MaxY	200

#define	MaxHelpLines	500

#define	MaxHighName	57
#define	MaxScores	7
typedef	struct
		{
			char	name[MaxHighName + 1];
			long	score;
			word	completed,episode;
		} HighScore;

#define	MaxGameName		32
#define	MaxSaveGames	6
typedef	struct
		{
			char	signature[4];
			word	*oldtest;
			boolean	present;
			char	name[MaxGameName + 1];
		} SaveGame;

#define	MaxString	128	// Maximum input string size

typedef	struct
		{
			int	x,y,
				w,h,
				px,py;
		} WindowRec;	// Record used to save & restore screen windows

typedef	enum
		{
			gd_Continue,
			gd_Easy,
			gd_Normal,
			gd_Hard
		} GameDiff;

//	Hack import for TED launch support
extern	boolean		tedlevel;
extern	int			tedlevelnum;
extern	void		TEDDeath(void);

extern	boolean		ingame,		// Set by game code if a game is in progress
					abortgame,	// Set if a game load failed
					loadedgame,	// Set if the current game was loaded
					NoWait,
					HighScoresDirty;
extern	GameDiff	restartgame;	// Normally gd_Continue, else starts game
extern	word		PrintX,PrintY;	// Current printing location in the window
extern	word		WindowX,WindowY,// Current location of window
					WindowW,WindowH;// Current size of window

extern	void		(*USL_MeasureString)(char *,word *,word *);
extern void				(*USL_DrawString)(char *);

extern	boolean		(*USL_SaveGame)(int),(*USL_LoadGame)(int);
extern	void		(*USL_ResetGame)(void);
extern	SaveGame	Games[MaxSaveGames];
extern	HighScore	Scores[];

#define	US_HomeWindow()	{PrintX = WindowX; PrintY = WindowY;}

extern	void	US_Startup(void),
				US_Setup(void),
				US_Shutdown(void);
void				US_InitRndT(boolean randomize);
void				US_SetLoadSaveHooks(boolean (*load)(int),
									boolean (*save)(int),
									void (*reset)(void));
void				US_TextScreen(void),
				US_UpdateTextScreen(void),
				US_FinishTextScreen(void);
void				US_DrawWindow(word x,word y,word w,word h);
void 				US_ClearWindow(void);
void				US_PrintCentered(char *s),
				US_CPrint(char *s),
				US_CPrintLine(char *s),
				US_Print(char *s);
void				US_PrintUnsigned(longword n);
void				US_PrintSigned(long n);
void				US_StartCursor(void),
				US_ShutCursor(void);
void				US_CheckHighScore(long score,word other);
void				US_DisplayHighScores(int which);
extern	boolean	US_UpdateCursor(void);
boolean US_LineInput(int x,int y,char *buf,char *def,boolean escok,
								int maxchars,int maxwidth);
extern	int		US_CheckParm(char *parm,char **strings);

		void	USL_PrintInCenter(char *s,Rect r);
		char 	*USL_GiveSaveName(word game);
#endif
