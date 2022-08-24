typedef struct Exporter	Exporter;
typedef struct Cursor Cursor;
typedef struct Postfxframehdr Postfxframehdr;
typedef struct Gfxctl Gfxctl;

struct Exporter
{
	int	fd;
	int cfd;
	Chan **roots;
	int	nroots;
};

struct Postfxframehdr {
	int        flags;
	vlong      nsec;
	ulong      chan;
	Rectangle  rect;
	Rectangle  dirtr;
	uchar     *data;
	long       ndata;
	uchar     *usrdata;
	long       nusrdata;
	Cursor     cursor;
	Point      mousexy;
	Rectangle  windows[1024];
	int        nwindows;
	int        windowsdirty;
};

struct Gfxctl
{
	Lock;
	Rendez        init;
	int           enabled;
	int           continuous;
	char         *shader;        /* shader to run */
	ulong         framecounter;  /* increments every new frame */
	vlong         timeout;       /* when to deactivate postfx */
	int           pipe[2];       /* generator uses pipe[0], shader uses pipe[1] */
	int           gpid;          /* generator PID */
	int           tpid;          /* timeout PID */
};

extern int			sniffctlfd; /*  fd for accessing/dev/draw/sniff (if it's there) */
extern char		*sniffsrc; /* filename used as source for sniff */
extern Exporter	exporter;
extern Memimage	*drawscreen;
extern Rectangle	drawrect;
extern int			drawpixelscalex;
extern int			drawpixelscaley;
extern Memimage	*pscreen;

extern Gfxctl           gfxctl;
extern Postfxframehdr  *postfxhdr;
extern int              postfxdebug;

/* fxtermdraw.c */
void fxtermdrawinit(void);
void setdrawcursor(Cursor* c);
void renderdrawcursor(void);
void setdrawpixelscale(int x, int y);
void deletedrawscreen(void);
void resetdrawscreen(void);
void flushdrawscreen(Rectangle r);
void flushpscreen(Rectangle r);
void dlock(void);
int candlock(void);
void dunlock(void);
void pfxlock(void);
void pfxunlock(void);

/* devdraw.c */
int drawgetriowindows(Rectangle windows[], int max, int *changed);
