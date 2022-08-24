/* auth.c */
extern	int		vncauth(Vnc*, char*);
extern	int		vnchandshake(Vnc*);
extern	int		vncsrvauth(Vnc*);
extern	int		vncsrvhandshake(Vnc*);

/* proto.c */
extern	Vnc*		vncinit(int, int, Vnc*);
extern	uchar		vncrdchar(Vnc*);
extern	ushort		vncrdshort(Vnc*);
extern	ulong		vncrdlong(Vnc*);
extern	Point		vncrdpoint(Vnc*);
extern	Rectangle	vncrdrect(Vnc*);
extern	Rectangle	vncrdcorect(Vnc*);
extern	Pixfmt		vncrdpixfmt(Vnc*);
extern	void		vncrdbytes(Vnc*, void*, int);
extern	char*		vncrdstring(Vnc*);
extern	char*	vncrdstringx(Vnc*);
extern	void		vncwrstring(Vnc*, char*);
extern  void    	vncgobble(Vnc*, long);

extern	void		vncflush(Vnc*);
extern	void		vncterm(Vnc*);
extern	void		vncwrbytes(Vnc*, void*, int);
extern	void		vncwrlong(Vnc*, ulong);
extern	void		vncwrshort(Vnc*, ushort);
extern	void		vncwrchar(Vnc*, uchar);
extern	void		vncwrpixfmt(Vnc*, Pixfmt*);
extern	void		vncwrrect(Vnc*, Rectangle);
extern	void		vncwrpoint(Vnc*, Point);

extern	void		vnclock(Vnc*);		/* for writing */
extern	void		vncunlock(Vnc*);

extern	void		hexdump(void*, int);

/* implemented by clients of the io library */
extern	void		vnchungup(Vnc*);

/* vncs.c */
void vnclistenerproc(void *arg);

/* rre.c */
int	countcorre(Vncs*, Rectangle);
int	counthextile(Vncs*, Rectangle);
int	countraw(Vncs*, Rectangle);
int	countrre(Vncs*, Rectangle);
int	sendcorre(Vncs*, Rectangle);
int	sendhextile(Vncs*, Rectangle);
int	sendraw(Vncs*, Rectangle);
int	sendrre(Vncs*, Rectangle);

/* rlist.c */
void addtorlist(Rlist*, Rectangle);
void freerlist(Rlist*);