typedef struct Cursor Cursor;

extern Cursor       cursor;
extern Cursor		arrow;
extern Memimage		*gscreen;
extern Point		cursorpos;

void		mouseresize(void);
Point 		mousexy(void);
void		cursoron(void);
void		cursoroff(void);
void		gsetcursor(Cursor*);
void		flushmemscreen(Rectangle r);
Rectangle	cursorrect(void);

extern QLock	drawlock;
void		drawactive(int);
void		getcolor(ulong, ulong*, ulong*, ulong*);
int		setcolor(ulong, ulong, ulong, ulong);
#define		TK2SEC(x)	0
extern void	blankscreen(int);
void		screeninit(int x, int y, int inchan, int outchan);
void		screenresize(int x, int y);
void		screenwin(void);
void		absmousetrack(int x, int y, int b, ulong msec);
Memdata*	attachscreen(Rectangle*, ulong*, int*, int*, int*);
void		deletescreenimage(void);
void		resetscreenimage(void);

void screenaddflush(Rectangle r);
void screenflush(void);


void		fsinit(char *mntpt, int x, int y, char *chanstr);
#define		ishwimage(i)	0
