#include <u.h>
#include <libc.h>
#include <draw.h>
#include <memdraw.h>

void
main(int argc, char* argv[])
{
	Memimage *img;    /* input image */
	ulong bcol;
	ulong rcol;  
	int y;
	
	if(memimageinit() != 0)
		sysfatal("memimageinit");
	img = readmemimage(0);
	if(img == nil)
		sysfatal("readmemimage");
	if(Dx(img->r) < 2)
		sysfatal("image must be at least 2 pixels wide");
	for(y = 0; y < Dy(img->r); y++){
		bcol = *wordaddr(img, Pt(0,y));
		rcol = *wordaddr(img, Pt(1,y));
		print("replace 0x%08ulx 0x%08ulx\n", bcol, rcol);
	}
	exits(nil);
}
