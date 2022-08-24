#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

void
main(int argc, char* argv[])
{
	Memimage *wimg;   /* 2x256 working image */
	Memimage *cimg;   /* 1x1 color image */
	Memimage *oimg;   /* output image */
	char bcl[11];     /* bcolor line */
	ulong col;        /* bcolor */
	int flags;        /* bflags */
	int i, s, n;
	
	if(memimageinit() != 0)
		sysfatal("memimageinit");
	wimg = allocmemimage(Rect(0, 0, 10, 256), RGBA32);
	memfillcolor(wimg, 0x000000ff);
	if(wimg == nil)
		sysfatal("error allocating working image");
	cimg = allocmemimage(Rect(0, 0, 1, 1), RGBA32);
	if(cimg == nil)
		sysfatal("error allocating color image");
	cimg->flags |= Frepl;
	cimg->clipr = wimg->r;
	for(i = 0; i < 256; i++){
		n = read(0, bcl, sizeof(bcl));
		if(n <= 0)
			break;
		if(bcl[10] != '\n')
			sysfatal("invalid input");
		bcl[10] = 0;
		col = strtoul(bcl, nil, 0);
		memfillcolor(cimg, col);
		memimagedraw(wimg, Rect(0,i,1+1,i+1), cimg, ZP, nil, ZP, S);
	}
	if(i == 0)
		exits(nil);
	oimg = allocmemimage(Rect(0, 0, 2, i), RGBA32);
	if(oimg == nil)
		sysfatal("error allocating output image");
	memimagedraw(oimg, oimg->r, wimg, ZP, nil, ZP, S);
	writememimage(1, oimg);
	exits(nil);
}
