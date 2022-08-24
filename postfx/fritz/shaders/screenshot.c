#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>
#include <cursor.h>
#include <mouse.h>
#include <thread.h>
#include "../../../include/fxterm.h"
#include "common.h"

char *pname = "screenshot";

Rectangle activerect;
Memimage *targetimage;



void
processframe(void)
{
	int fd;

	if(!eqrect(frame->dirtr, frame->rect))
		return;

	fd = open("/tmp/_fxterm.bit", OWRITE);
	if(fd < 0){
		fprint(2, "%s: failed to open file: %r\n", pname);
		return;
	}
	writememimage(fd, frameimage);
	close(fd);
}

void
usage(void)
{
	fprint(2, "usage: screenshot\n");
	threadexitsall("usage");
}

void
shadersetup(void)
{

}

void
threadmain(int argc, char* argv[])
{
	ARGBEGIN{
	default:
		usage();
	}ARGEND
	mainloop();
	threadexitsall(nil);
}
