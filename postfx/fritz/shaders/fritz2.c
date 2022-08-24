#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "fritz2";

/* program arguments */
char       *wallpaperfile;
char       *backgroundfile;

Memimage   *background;
Memimage   *wallpaper;

static void
updatebackimage(Rectangle rect)
{
	uchar *bp1, *bp2, *bp3, *bp4; /* byte pointers */
	Pixel *pp1, *pp2, *pp3, *pp4; /* pixel pointers */
	ulong *cp1, *cp2, *cp3, *cp4; /* color pointers */
	long nbytes;
	vlong lso; /* line start offset */
	double r, g, b;
	int y, i;
	double B, P;
	uchar c;
	int f; /* from infoimage: flags */
	int id; /* from infoimage: window id */

	nbytes = Dx(rect)*sizeof(ulong);
	for(y = rect.min.y; y < rect.max.y; y++){
		lso = y*sizeof(ulong)*Dx(frame->rect)+rect.min.x*sizeof(ulong);
		bp1 = frame->data + lso;
		bp2 = byteaddr(background, ZP) + lso;
		if(wallpaper != nil)
			bp3 = byteaddr(wallpaper, ZP) + lso;
		else
			bp3 = nil;
		bp4 = byteaddr(infoimage, ZP) + lso;
		for(i = 0; i < nbytes; i += sizeof(ulong)){
			pp1 = (Pixel*)(bp1+i);
			pp2 = (Pixel*)(bp2+i);
			if(bp3 != nil)
				pp3 = (Pixel*)(bp3+i);
			pp4 = (Pixel*)(bp4+i);
	
			cp1 = (ulong*)pp1;
			cp2 = (ulong*)pp2;
			cp3 = (ulong*)pp3;
			cp4 = (ulong*)pp4;		

			id = (*cp4 >> 8);
			f = *cp4 & 0xFF;

				
//			*cp1 = *cp4; continue;
	
			if(f & Fborder){
				if(*cp1 == Crioborder)
					*cp1 = 0x000000;					
				else if(*cp1 == Criolightborder)
					*cp1 = 0x000000;
				else if(*cp1 == Criomenuborder)
					*cp1 = 0xff00ff;
				else if(*cp1 == Crioselectborder)
					*cp1 = 0x00ff00;
			} else if(f == 0){
				if(cp3 != nil)
					*cp1 = *cp3;
			} else {
/*
				if(*cp4 & Pmenu){
					c = (pp1->r+pp1->g+pp1->b)/3;
					pp1->r = 0; 
					pp1->g = c/2; 
					pp1->b = c; 
				}
*/
				if(id != activewindowid){
					c = (pp1->r+pp1->g+pp1->b)/5;
					pp1->r = c; 
					pp1->g = c; 
					pp1->b = c; 
				} 
				r = pp1->r;
				g = pp1->g;
				b = pp1->b;
				B = pp2->r; 
				P = 0.5+B/255;
				r *= P;
				g *= P;
				b *= P;
				pp1->r = clampd(r, 0, 255);
				pp1->g = clampd(g, 0, 255);
				pp1->b = clampd(b, 0, 255);
			}
	
/*
			if(*cp4 & Pshadow){
				pp1->r /= 2; 
				pp1->g /= 2;
				pp1->b /= 2; 
			}
*/

		}
	}
}

void
processframe(void)
{
	if(!eqrect(frame->dirtr, ZR)){
		findactivewindow();
		work(frame->dirtr, updatebackimage);	
	}
}

void
usage(void)
{
	fprint(2, "usage: tint.pfx [-w wallpaper] background\n");
	threadexitsall("usage");
}

void
shadersetup(void)
{
	char *filenames[2];
	Memimage *images[2];
	Memimage *tmpimg;
	long l, m, n;
	int fd, i;

	images[0] = 0;
	images[1] = 0;
	filenames[0] = wallpaperfile;
	filenames[1] = backgroundfile;
	for(i = 0; i < 2; i++){
		if(filenames[i] == nil)
			continue;
		fd = open(filenames[i], OREAD);
		if(fd < 0){
			fprint(2, "can't open %s: %r", filenames[i]);
			threadexitsall("open");
		}
		tmpimg = readmemimage(fd);
		if(tmpimg == nil){
			fprint(2, "can't read image %s: %r", filenames[i]);
			threadexitsall("readmemimage");
		}
		close(fd);
		if(tmpimg->chan != XRGB32){
			fprint(2, "%s not XRGB32 image\n", filenames[i]);
			threadexitsall("chan");
		}
		tmpimg->flags |= Frepl;
		tmpimg->clipr = Rect(-0x3FFFFFF, -0x3FFFFFF, 0x3FFFFFF, 0x3FFFFFF);
		images[i] = allocmemimage(frame->rect, frame->chan);
		memimagedraw(images[i], images[i]->r, tmpimg, tmpimg->r.min, nil, ZP, S);
		freememimage(tmpimg);
	}
	wallpaper = images[0];
	background = images[1];
}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	case 'w': 
		wallpaperfile = ARGF();
		if(wallpaperfile == nil)
			usage();
		break;
	default:
		usage();
	}ARGEND
	if(argc != 1)
		usage();
	backgroundfile = argv[0];
	mainloop();
	threadexitsall(nil);
}
