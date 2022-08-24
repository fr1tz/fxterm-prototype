#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "neoncursor";

Memimage   *backimage;
Memimage   *cursorimage;
Memimage   *cursorimagemask;
Cursor      lastcursor;

Cursor	arrow = {
	{ -1, -1 },
	{ 0xFF, 0xFF, 0x80, 0x01, 0x80, 0x02, 0x80, 0x0C,
	  0x80, 0x10, 0x80, 0x10, 0x80, 0x08, 0x80, 0x04,
	  0x80, 0x02, 0x80, 0x01, 0x80, 0x02, 0x8C, 0x04,
	  0x92, 0x08, 0x91, 0x10, 0xA0, 0xA0, 0xC0, 0x40,
	},
	{ 0x00, 0x00, 0x7F, 0xFE, 0x7F, 0xFC, 0x7F, 0xF0,
	  0x7F, 0xE0, 0x7F, 0xE0, 0x7F, 0xF0, 0x7F, 0xF8,
	  0x7F, 0xFC, 0x7F, 0xFE, 0x7F, 0xFC, 0x73, 0xF8,
	  0x61, 0xF0, 0x60, 0xE0, 0x40, 0x40, 0x00, 0x00,
	},
};

Cursor neonarrow = {
	{-1, -1},
	{ 0x00, 0x00, 0x70, 0x00, 0x7c, 0x00, 0x7f, 0x00,
	  0x3f, 0xc0, 0x3f, 0xf0, 0x1f, 0xfc, 0x1f, 0xfe,
	  0x0f, 0x00, 0x0f, 0x00, 0x07, 0x00, 0x07, 0x00,
	  0x03, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00,
	},
	{ 0xf0, 0x00, 0x8c, 0x00, 0x83, 0x00, 0x80, 0xc0,
	  0x40, 0x30, 0x40, 0x0c, 0x20, 0x02, 0x20, 0x01,
	  0x10, 0xfe, 0x10, 0x80, 0x08, 0x80, 0x08, 0x80,
	  0x04, 0x80, 0x04, 0x80, 0x02, 0x80, 0x01, 0x00,
	},
};

static Rectangle
cursorrect(void)
{
	Rectangle r;

	r.min.x = frame->mousexy.x + frame->cursor.offset.x;
	r.min.y = frame->mousexy.y + frame->cursor.offset.y;
	r.max.x = r.min.x + 16;
	r.max.y = r.min.y + 16;
	return r;
}

static void
updatecursorimage(Cursor *c)
{
	Rectangle r;
	uchar mask[2*16];
	Memimage* clrmask;
	Memimage* setmask;

	if(memcmp(c, &arrow, sizeof(Cursor)) == 0)
		c = &neonarrow;

	r = Rect(0, 0, 16, 16);
	clrmask = allocmemimage(r, GREY1);
	loadmemimage(clrmask, r, c->clr, 2*16);
	setmask = allocmemimage(r, GREY1);
	loadmemimage(setmask, r, c->set, 2*16);
	
	memfillcolor(cursorimage, 0x00000000);
	*brushcol = 0xffff00;
	memimagedraw(cursorimage, r, memwhite, ZP, clrmask, ZP, SoverD);
	memimagedraw(cursorimage, r, brushimg, ZP, setmask, ZP, SoverD);
	memimagedraw(cursorimagemask, r, memblack, ZP, nil, ZP, S);
	memimagedraw(cursorimagemask, r, memwhite, ZP, clrmask, ZP, SoverD);
	memimagedraw(cursorimagemask, r, memwhite, ZP, setmask, ZP, SoverD);
	freememimage(clrmask);
	freememimage(setmask);
}

void
processframe(void)
{
	static Rectangle ldcr; /* last drawn cursor rect */
	int x, y;
	Point p0, p1;
	vlong t1, t2;
	Rectangle cr; /* cursor rect */
	int dc; /* draw cursor? */

	t1 = nsec();

	if(eqrect(frame->dirtr, ZR) == 0)
		memimagedraw(backimage, frame->dirtr, frameimage, frame->dirtr.min, 0, ZP, S);

	cr = cursorrect();
	dc = 0;
	if(memcmp(&lastcursor, &frame->cursor, sizeof(Cursor)) != 0){
		updatecursorimage(&frame->cursor);
		memcpy(&lastcursor, &frame->cursor, sizeof(Cursor));
		dc = 1;
	}
	if(rectXrect(cr, frame->dirtr) == 1 || eqrect(ldcr, cr) == 0){
		if(eqrect(frame->dirtr, ZR))
			frame->dirtr = cr;
		else
			combinerect(&frame->dirtr, cr);
		if(!eqrect(ldcr, ZR))
			combinerect(&frame->dirtr, ldcr);
		memimagedraw(frameimage, frame->dirtr, backimage, frame->dirtr.min, 0, ZP, S);
		rectclip(&frame->dirtr, frame->rect);
		dc = 1;
	}
	if(dc){
		memimagedraw(frameimage, cr, cursorimage, ZP, cursorimagemask, ZP, SoverD);
		ldcr = cr;
	}

/*
	*brushcol = 0xff0000;
	x = frame->mousexy.x;
	y = frame->mousexy.y;
	p0.x = x;
	p0.y = frame->rect.min.y;
	p1.x = x;
	p1.y = frame->rect.max.y;
	memimageline(frameimage, p0, p1, 0, 0, 0, brushimg, ZP, S);
	p0.x = frame->rect.min.x;
	p0.y = y;
	p1.x = frame->rect.max.x;
	p1.y = y;
	memimageline(frameimage, p0, p1, 0, 0, 0, brushimg, ZP, S);
*/

	t2 = nsec();
//	fprint(2, "stdcursor: processing time: %d\n", t2-t1);
}

void
shadersetup(void)
{
	backimage = allocmemimage(frame->rect, frame->chan);
	if(backimage == nil){
		fprint(2, "%s: unable to allocate backimage: %r\n", pname);
		threadexitsall("allocmemimage");
	}
	cursorimage = allocmemimage(Rect(0,0,16,16), XRGB32);
	if(cursorimage == nil){
		fprint(2, "Unable to allocate cursorimage: %r\n");
		threadexitsall("allocmemimage");
	}
	cursorimagemask = allocmemimage(Rect(0,0,16,16), XRGB32);
	if(cursorimagemask == nil){
		fprint(2, "Unable to allocate cursorimagemask: %r\n");
		threadexitsall("allocmemimage");
	}
}

void
threadmain(int argc, char* argv[])
{
	argv0 = argv[0];
	mainloop();
	threadexitsall(nil);
}
