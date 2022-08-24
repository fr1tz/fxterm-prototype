#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "finish";

enum { /* image scaling modes */
	Stile = 0,
	Slandscape,
	Sportrait
};

/* program arguments */
int         scalemode;
int         tflag;
int         lflag;
char       *wallpaperfile;
char       *backgroundfile;

Memimage   *background;
Memimage   *wallpaper;

#define K2 7	/* from -.7 to +.7 inclusive, meaning .2 into each adjacent pixel */
#define NK (2*K2+1)
double K[NK];

double
fac(int L)
{
	int i, f;

	f = 1;
	for(i=L; i>1; --i)
		f *= i;
	return f;
}

/* 
 * i0(x) is the modified Bessel function, Σ (x/2)^2L / (L!)²
 * There are faster ways to calculate this, but we precompute
 * into a table so let's keep it simple.
 */
double
i0(double x)
{
	double v;
	int L;

	v = 1.0;
	for(L=1; L<10; L++)
		v += pow(x/2., 2*L)/pow(fac(L), 2);
	return v;
}

double
kaiser(double x, double τ, double α)
{
	if(fabs(x) > τ)
		return 0.;
	return i0(α*sqrt(1-(x*x/(τ*τ))))/i0(α);
}

void
resamplex(uchar *in, int off, int d, int inx, uchar *out, int outx)
{
	int i, x, k;
	double X, xx, v, rat;


	rat = (double)inx/(double)outx;
	for(x=0; x<outx; x++){
		if(inx == outx){
			/* don't resample if size unchanged */
			out[off+x*d] = in[off+x*d];
			continue;
		}
		v = 0.0;
		X = x*rat;
		for(k=-K2; k<=K2; k++){
			xx = X + rat*k/10.;
			i = xx;
			if(i < 0)
				i = 0;
			if(i >= inx)
				i = inx-1;
			v += in[off+i*d] * K[K2+k];
		}
		out[off+x*d] = v;
	}
}

void
resampley(uchar **in, int off, int iny, uchar **out, int outy)
{
	int y, i, k;
	double Y, yy, v, rat;

	rat = (double)iny/(double)outy;
	for(y=0; y<outy; y++){
		if(iny == outy){
			/* don't resample if size unchanged */
			out[y][off] = in[y][off];
			continue;
		}
		v = 0.0;
		Y = y*rat;
		for(k=-K2; k<=K2; k++){
			yy = Y + rat*k/10.;
			i = yy;
			if(i < 0)
				i = 0;
			if(i >= iny)
				i = iny-1;
			v += in[i][off] * K[K2+k];
		}
		out[y][off] = v;
	}

}

int
max(int a, int b)
{
	if(a > b)
		return a;
	return b;
}

Memimage*
resample(int xsize, int ysize, Memimage *m)
{
	int i, j, d, bpl, nchan;
	Memimage *new;
	uchar **oscan, **nscan;

	new = allocmemimage(Rect(0, 0, xsize, ysize), m->chan);
	if(new == nil)
		sysfatal("can't allocate new image: %r");

	oscan = malloc(Dy(m->r)*sizeof(uchar*));
	nscan = malloc(max(ysize, Dy(m->r))*sizeof(uchar*));
	if(oscan == nil || nscan == nil)
		sysfatal("can't allocate: %r");

	/* unload original image into scan lines */
	bpl = bytesperline(m->r, m->depth);
	for(i=0; i<Dy(m->r); i++){
		oscan[i] = malloc(bpl);
		if(oscan[i] == nil)
			sysfatal("can't allocate: %r");
		j = unloadmemimage(m, Rect(m->r.min.x, m->r.min.y+i, m->r.max.x, m->r.min.y+i+1), oscan[i], bpl);
		if(j != bpl)
			sysfatal("unloadmemimage");
	}

	/* allocate scan lines for destination. we do y first, so need at least Dy(m->r) lines */
	bpl = bytesperline(Rect(0, 0, xsize, Dy(m->r)), m->depth);
	for(i=0; i<max(ysize, Dy(m->r)); i++){
		nscan[i] = malloc(bpl);
		if(nscan[i] == nil)
			sysfatal("can't allocate: %r");
	}

	/* resample in X */
	nchan = d = m->depth/8;
	if(m->chan == XRGB32)
		nchan--;
	for(i=0; i<Dy(m->r); i++){
		for(j=0; j<nchan; j++)
			resamplex(oscan[i], j, d, Dx(m->r), nscan[i], xsize);
		free(oscan[i]);
		oscan[i] = nscan[i];
		nscan[i] = malloc(bpl);
		if(nscan[i] == nil)
			sysfatal("can't allocate: %r");
	}

	/* resample in Y */
	for(i=0; i<xsize; i++)
		for(j=0; j<nchan; j++)
			resampley(oscan, d*i+j, Dy(m->r), nscan, ysize);

	/* pack data into destination */
	bpl = bytesperline(new->r, m->depth);
	for(i=0; i<ysize; i++){
		j = loadmemimage(new, Rect(0, i, xsize, i+1), nscan[i], bpl);
		if(j != bpl)
			sysfatal("loadmemimage: %r");
	}
	return new;
}

static void
update(Memimage *S, Memimage *I, Memimage *W, Memimage *T, Rectangle r, Memimage *D)
{
	/* argument / variable suffixes:
	 * S = source image
	 * I = info image
	 * W = wallpaper image
	 * T = window background image
	 * r = area to update
	 * D = destination image
	*/
	ulong *lsS, *lsI, *lsW, *lsB, *lsD;  /* line start pointers */
	ulong *pS, *pI, *pW, *pB, *pD;      /* pixel pointers */
	uchar *cS, *cI, *cW, *cB, *cD;      /* channel pointers */
	int w;                         /* in/out image width */
	int x, y;                      /* current pixel x/y coordinates */
	int f;                          /* from infoimage: flags */
	int id;                         /* frim infoimage: window id */
	int rD, gD, bD;

	w = Dx(I->r);
	x = r.min.x;
	y = r.min.y;
	lsS = (ulong*)byteaddr(S, r.min);
	lsI = (ulong*)byteaddr(I, r.min);
	lsW = (ulong*)byteaddr(W, r.min);
	lsB = (ulong*)byteaddr(T, r.min);
	lsD = (ulong*)byteaddr(D, r.min);
	pS = lsS;
	pI = lsI;
	pW = lsW;
	pB = lsB;
	pD = lsD;
	while(y < r.max.y){
		cS = (uchar*)pS;
		cI = (uchar*)pI;
		cW = (uchar*)pW;
		cB = (uchar*)pB;
		cD = (uchar*)pD;

		f = (*pI & 0xFF);

		rD = cS[R];
		gD = cS[G];
		bD = cS[B];

		if(f & Fborder)
			goto scanlines;

		if(f == 0 && wallpaper != nil) {
			rD = cW[R];
			gD = cW[G];
			bD = cW[B];
		} else if(f & Fwindow){
			if(tflag){
				rD = cS[R] * cB[R] / 255;
				gD =cS[G] * cB[G] / 255;
				bD = cS[B] * cB[B] / 255;
			}else{
				rD += cB[R];
				gD += cB[G];
				bD += cB[B];
			}
		}

scanlines:
		if(lflag && y & 1){
			rD= cD[R]-cD[R]/4;
			gD = cD[G]-cD[G]/4;
			bD = cD[B]-cD[B]/4;
		}
next:

		if(rD > 255)
			rD = 255;
		if(gD > 255)
			gD = 255;
		if(bD > 255)
			bD = 255;
		cD[R] = rD;
		cD[G] = gD;
		cD[B] = bD;

		if(x == r.max.x-1){
			x = r.min.x;
			y++;
			lsS += w;
			lsI += w;
			lsW += w;
			lsB += w;
			lsD += w;
			pS = lsS;
			pI = lsI;
			pW = lsW;
			pB = lsB;
			pD = lsD;
		} else {
			x++;
			pS++;
			pI++;
			pW++;
			pB++;
			pD++;
		}
	}
}

static void
pass1(Rectangle r)
{
	update(frameimage, infoimage, wallpaper, background, r, frameimage);
}

void
processframe(void)
{
	if(eqrect(frame->dirtr, ZR) == 0){
		rectclip(&frame->dirtr, frame->rect);
		work(frame->dirtr, pass1);
	}
}

void
usage(void)
{
	fprint(2, "usage: finish TODO\n");
	threadexitsall("usage");
}

void
shadersetup(void)
{
	char *filenames[2];
	Memimage *images[2];
	Memimage *ti1, *ti2;
	long l, m, n;
	int fd, i;

	filenames[0] = wallpaperfile;
	filenames[1] = backgroundfile;
	for(i = 0; i < 2; i++){
		images[i] = allocmemimage(frame->rect, frame->chan);
		if(filenames[i] == nil)
			continue;
		fd = open(filenames[i], OREAD);
		if(fd < 0){
			fprint(2, "can't open %s: %r", filenames[i]);
			threadexitsall("open");
		}
		ti1 = readmemimage(fd);
		if(ti1 == nil){
			fprint(2, "can't read image %s: %r", filenames[i]);
			threadexitsall("readmemimage");
		}
		close(fd);
		fprint(2, "swag\n");
//		ti2 = resample(Dx(frame->rect), Dy(frame->rect), ti1);
		memimagedraw(images[i], images[i]->r, ti1, ti1->r.min, nil, ZP, S);
		freememimage(ti1);
		freememimage(ti2);
	}
	wallpaper = images[0];
	background = images[1];
}

void
threadmain(int argc, char* argv[])
{
	char *s;

	ARGBEGIN{
	case 't': 
		tflag = 1;
		break;
	case 'l': 
		lflag = 1;
		break;
	case 's':
		s = ARGF();
		if(s == nil)
			usage();
		if(strcmp(s, "l") == 0)
			scalemode = Slandscape;
		else if(strcmp(s, "p") == 0)
			scalemode = Sportrait;
		else {
			fprint(2, "%s: %s: invalid scale mode\n", pname, s);
			threadexitsall("scalemode");
		}
		break;
	case 'w': 
		wallpaperfile = ARGF();
		if(wallpaperfile == nil)
			usage();
		break;
	case 'b': 
		backgroundfile = ARGF();
		if(backgroundfile == nil)
			usage();
		break;
	default:
		usage();
	}ARGEND
	mainloop();
	threadexitsall(nil);
}
