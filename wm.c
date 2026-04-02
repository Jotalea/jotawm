#include <err.h>
#include <signal.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>

#define MODMASK  Mod4Mask
#define NUM_WS   9
#define MAX_WIN  64

static const char *termcmd[] = { "st",        NULL };
static const char *menucmd[] = { "dmenu_run", NULL };

typedef union  { int i; float f; const char **v; } Arg;
typedef struct { unsigned int mod; KeySym sym; void (*fn)(const Arg *); Arg arg; } Key;

// Avaliable keys[] functions
static void focusdir(const Arg *);
static void killclient(const Arg *);
static void moveclient(const Arg *);
static void movetows(const Arg *);
static void quit(const Arg *);
static void setmfact(const Arg *);
static void spawn(const Arg *);
static void togglefs(const Arg *);
static void view(const Arg *);

#define WS(n) \
    { MODMASK,           XK_##n, view,     {.i = n-1} }, \
    { MODMASK|ShiftMask, XK_##n, movetows, {.i = n-1} }

static Key keys[] = {
    { MODMASK,           XK_Return, spawn,      {.v = termcmd} },
    { MODMASK,           XK_space,  spawn,      {.v = menucmd} },
    { MODMASK,           XK_j,      focusdir,   {.i = +1} },
    { MODMASK,           XK_k,      focusdir,   {.i = -1} },
    { MODMASK|ShiftMask, XK_j,      moveclient, {.i = +1} },
    { MODMASK|ShiftMask, XK_k,      moveclient, {.i = -1} },
    { MODMASK,           XK_h,      setmfact,   {.f = -0.05f} },
    { MODMASK,           XK_l,      setmfact,   {.f = +0.05f} },
    { MODMASK,           XK_q,      killclient, {0} },
    { MODMASK|ShiftMask, XK_q,      quit,       {0} },
    { MODMASK,           XK_f,      togglefs,   {0} },
    WS(1),WS(2),WS(3),WS(4),WS(5),WS(6),WS(7),WS(8),WS(9),
};

typedef struct { Window win; int ws, fs; } Client;

static Display *dpy;
static Window   root;
static int      sw, sh, nclients, curws, running = 1, sel = -1;
static float    mfact = 0.5f;
static Client   pool[MAX_WIN];

static void
arrange(void)
{
    int n = 0, i = 0, mw;
    for (int j = 0; j < nclients; j++)
        if (pool[j].ws == curws) n++;
    mw = n <= 1 ? sw : (int)(sw * mfact);
    for (int j = 0; j < nclients; j++) {
        Client *c = &pool[j];
        if (c->ws != curws) { XMoveWindow(dpy, c->win, sw * 2, 0); continue; }
        if (c->fs)          { XMoveResizeWindow(dpy, c->win, 0, 0, sw, sh); XRaiseWindow(dpy, c->win); }
        else if (i == 0)      XMoveResizeWindow(dpy, c->win, 0, 0, mw, sh);
        else                { int bh = sh / (n-1); XMoveResizeWindow(dpy, c->win, mw, (i-1)*bh, sw-mw, bh); }
        i++;
    }
    XSync(dpy, False);
}

static void
focus(int idx)
{
    sel = (idx >= 0 && idx < nclients && pool[idx].ws == curws) ? idx : -1;
    if (sel >= 0) { XSetInputFocus(dpy, pool[sel].win, RevertToPointerRoot, CurrentTime); XRaiseWindow(dpy, pool[sel].win); }
    else            XSetInputFocus(dpy, root,           RevertToPointerRoot, CurrentTime);
}

static void
focusdir(const Arg *a)
{
    if (!nclients) return;
    int dir = a->i > 0 ? 1 : -1;
    int i   = sel >= 0 ? (sel + dir + nclients) % nclients : 0;
    for (int n = 0; n < nclients; n++, i = (i + dir + nclients) % nclients)
        if (pool[i].ws == curws) { focus(i); return; }
}

static void
moveclient(const Arg *a)
{
    if (sel < 0 || !nclients) return;
    int dir = a->i > 0 ? 1 : -1, j = sel;
    for (int n = 0; n < nclients; n++) {
        j = (j + dir + nclients) % nclients;
        if (pool[j].ws == curws) break;
    }
    if (j == sel) return;
    Client tmp = pool[sel]; pool[sel] = pool[j]; pool[j] = tmp;
    sel = j;
    arrange();
}

static void killclient(const Arg *a) { (void)a; if (sel>=0) XKillClient(dpy, pool[sel].win); }
static void quit(const Arg *a)       { (void)a; running = 0; }
static void togglefs(const Arg *a)   { (void)a; if (sel>=0) { pool[sel].fs ^= 1; arrange(); } }
static void setmfact(const Arg *a)   { mfact += a->f; if (mfact<0.1f) mfact=0.1f; if (mfact>0.9f) mfact=0.9f; arrange(); }
static void spawn(const Arg *a)      { if (fork()==0) { if (dpy) close(ConnectionNumber(dpy)); setsid(); execvp(((char**)a->v)[0],(char**)a->v); err(1,"execvp %s",((char**)a->v)[0]); } }

static void
view(const Arg *a)
{
    if (a->i == curws || a->i < 0 || a->i >= NUM_WS) return;
    curws = a->i; arrange(); focusdir(&(Arg){.i=+1});
}

static void
movetows(const Arg *a)
{
    if (sel < 0 || a->i < 0 || a->i >= NUM_WS || a->i == curws) return;
    pool[sel].ws = a->i; arrange(); focusdir(&(Arg){.i=+1});
}

static int
winfind(Window w)
{
    for (int i = 0; i < nclients; i++)
        if (pool[i].win == w) return i;
    return -1;
}

static void
destroynotify(XEvent *e)
{
    int i = winfind(e->xdestroywindow.window);
    if (i < 0) return;
    pool[i] = pool[--nclients];
    if      (sel == i)        sel = -1;
    else if (sel == nclients) sel = i;
    arrange();
    focusdir(&(Arg){.i=+1});
}

static void
maprequest(XEvent *e)
{
    XWindowAttributes wa;
    if (!XGetWindowAttributes(dpy, e->xmaprequest.window, &wa) || wa.override_redirect) return;
    if (nclients >= MAX_WIN) { return; }
    pool[nclients] = (Client){ e->xmaprequest.window, curws, 0 };
    XSelectInput(dpy, pool[nclients].win, EnterWindowMask | StructureNotifyMask);
    XMapWindow(dpy, pool[nclients].win);
    focus(nclients++);
    arrange();
}

static void
enternotify(XEvent *e)
{
    int i;
    if (e->xcrossing.mode != NotifyNormal || e->xcrossing.detail == NotifyInferior) return;
    if ((i = winfind(e->xcrossing.window)) >= 0 && pool[i].ws == curws) focus(i);
}

static void
keypress(XEvent *e)
{
    KeySym sym = XLookupKeysym(&e->xkey, 0);
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
        if (sym == keys[i].sym && keys[i].mod == e->xkey.state && keys[i].fn)
            keys[i].fn(&keys[i].arg);
}

int
main(void)
{
    XEvent ev;
    if (!(dpy = XOpenDisplay(NULL))) errx(1, "cannot open display");
    signal(SIGCHLD, SIG_IGN);
#ifdef __OpenBSD__
    if (pledge("stdio rpath proc exec", NULL) == -1) err(1, "pledge");
#endif
    root = DefaultRootWindow(dpy);
    sw   = DisplayWidth(dpy,  DefaultScreen(dpy));
    sh   = DisplayHeight(dpy, DefaultScreen(dpy));
    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++)
        XGrabKey(dpy, XKeysymToKeycode(dpy, keys[i].sym), keys[i].mod, root, True, GrabModeAsync, GrabModeAsync);
    while (running && !XNextEvent(dpy, &ev)) {
        if      (ev.type == MapRequest)    maprequest(&ev);
        else if (ev.type == DestroyNotify) destroynotify(&ev);
        else if (ev.type == KeyPress)      keypress(&ev);
        else if (ev.type == EnterNotify)   enternotify(&ev);
    }
    XCloseDisplay(dpy);
    return 0;
}
