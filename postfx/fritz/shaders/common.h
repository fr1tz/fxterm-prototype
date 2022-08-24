enum /* Constants */
{
	STACK = 2048,
	Criomenuhigh = 0x448844,
	Criomenuborder = 0x88CC88,
	Criomenuback = 0xEAFFEA,
	Crioborder = 0x55AAAA,
	Criolightborder = 0x9EEEEE,
	Crioselectborder = 0xFF0000,
	X = 3,
	R = 2,
	G = 1,
	B = 0,
};

enum /* infoimage flags */
{
	Fwindow = 0x01,
	Fborder = 0x02,
};

typedef struct Postfxframe Postfxframe;
typedef struct Pixel Pixel;
typedef void (*Jobfunc)(Rectangle);

struct Pixel { /* TODO: get rid of this */
	u8int b;
	u8int g;
	u8int r;
	u8int x;
};

extern char        *pname;
extern Postfxframe *frame;
extern Memimage    *frameimage;
extern Memimage    *infoimage;
extern int          activewindowid;
extern int          menuwindowid;
extern int          infoimageseq;
extern Memimage    *brushimg;
extern ulong       *brushcol;

double clampd(double val, double min, double max);
int clampi(int val, int min, int max);
ulong inputpixel(Point pt);
ulong backpixel(Point pt);
ulong luma(uint p);
void resize(Memimage *dst, Rectangle r, Memimage *src, Rectangle sr);
void blur(Memimage *src, Rectangle rect, Memimage *dst, int vertical);
void mainloop(void);
void findactivewindow(void);
void processframe(void);
void shadersetup(void);
void work(Rectangle r, Jobfunc func);
