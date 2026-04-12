#include <err.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/keysym.h>
#include <X11/Xlib.h>
#include <X11/XF86keysym.h>

#include "jwm.h"

#define SPLITRATIO 0.5f
#define NELEM(a)   (sizeof(a) / sizeof(*(a)))

typedef struct { Window win; int isfull; } Client;

/* Forward Declarations */
static int xerror(Display *d, XErrorEvent *e);
static void tile(void);
static int rmclient(Window w);

/* Global State */
static Display *dpy;
static Window   root;
static int      scrw, scrh, curspace, running = 1;
static float    splitratio = SPLITRATIO;
static Client   clients[NSPACE][NCLIENT];
static int      nclients[NSPACE], selclient[NSPACE];

static int xerror(Display *d, XErrorEvent *e) {
    /* Safely ignore all asynchronous X11 protocol errors */
    return 0; (void) d, (void)e;
}

static void tile(void) {
    for (int s = 0; s < NSPACE; s++) {
        int nc = nclients[s];
         
        if (nc > 0) { selclient[s] %= nc; }

        for (int i = 0; i < nc; i++) {
            Client *c = &clients[s][i];

            if (s != curspace) {
                XMoveWindow(dpy, c->win, scrw * 2, 0);
                continue;
            }

            if (c->isfull) {
                XMoveResizeWindow(dpy, c->win, 0, BARH, scrw, scrh);
                continue;
            }

            int mastw  = (nc > 1) ? (int)(scrw * splitratio) : scrw;
            int stackc = (nc > 1) ? (nc - 1) : 1;
             
            int cx = i ? mastw : 0;
            int cy = (i ? (i - 1) * (scrh / stackc) : 0) + BARH;
            int cw = i ? (scrw - mastw) : mastw;
            int ch = i ? (scrh / stackc) : scrh;

            XMoveResizeWindow(dpy, c->win, cx, cy, cw, ch);
        }
    }

    Client *f = nclients[curspace] ? &clients[curspace][selclient[curspace]] : NULL;
    XSetInputFocus(dpy, f ? f->win : root, RevertToPointerRoot, CurrentTime);
     
    if (f) { XRaiseWindow(dpy, f->win); }
     
    XSync(dpy, False);
}

static int rmclient(Window w) {
    for (int s = 0; s < NSPACE; s++) {
        for (int i = 0; i < nclients[s]; i++) {
            if (clients[s][i].win == w) {
                nclients[s]--;
                clients[s][i] = clients[s][nclients[s]];
                return 1;
            }
        }
    }
    return 0;
}

int main(void) {
    XEvent ev;

    if (!(dpy = XOpenDisplay(NULL))) { errx(1, "cannot open display"); }

    XSetErrorHandler(xerror);

#ifdef __OpenBSD__
    pledge("stdio proc exec", NULL);
#endif

    root = DefaultRootWindow(dpy);
    
    XSetWindowBackground(dpy, root, BlackPixel(dpy, DefaultScreen(dpy)));
    XClearWindow(dpy, root);
    
    scrw = DisplayWidth(dpy, 0);
    scrh = DisplayHeight(dpy, 0) - BARH;

    XSelectInput(dpy, root, SubstructureRedirectMask | SubstructureNotifyMask | KeyPressMask);

    for (size_t i = 0; i < NELEM(keys); i++) {
        XGrabKey(dpy, XKeysymToKeycode(dpy, keys[i].sym), keys[i].mod,
            root, True, GrabModeAsync, GrabModeAsync);
    }
     
    signal(SIGCHLD, SIG_IGN);

    while (running && !XNextEvent(dpy, &ev)) {
        switch (ev.type) {
        case MapRequest: {
            XWindowAttributes wa;
            Window w = ev.xmaprequest.window;
             
            if (nclients[curspace] >= NCLIENT) {  
                XDestroyWindow(dpy, w);
                break;  
            }
            if (!XGetWindowAttributes(dpy, w, &wa) || wa.override_redirect) { break; }
             
            clients[curspace][nclients[curspace]] = (Client){ w, 0 };
            XSelectInput(dpy, w, EnterWindowMask | StructureNotifyMask);
            
            XGrabButton(dpy, Button1, AnyModifier, w, False, ButtonPressMask, GrabModeSync, GrabModeAsync, None, None);
             
            selclient[curspace] = nclients[curspace];
            nclients[curspace]++;
            
            tile();
            XMapWindow(dpy, w);
            break;
        }
        case DestroyNotify:
            if (rmclient(ev.xdestroywindow.window)) { tile(); }
            break;
        case UnmapNotify:
            if (rmclient(ev.xunmap.window)) { tile(); }
            break;
        case EnterNotify:
            if (ev.xcrossing.mode != NotifyNormal || ev.xcrossing.detail == NotifyInferior) { break; }
             
            for (int i = 0; i < nclients[curspace]; i++) {
                if (clients[curspace][i].win == ev.xcrossing.window) {
                    selclient[curspace] = i;
                    tile();
                    break;
                }
            }
            break;
        case ButtonPress:
            for (int i = 0; i < nclients[curspace]; i++) {
                if (clients[curspace][i].win == ev.xbutton.window) {
                    if (selclient[curspace] != i) {
                        selclient[curspace] = i;
                        tile();
                    }
                    break;
                }
            }
            XAllowEvents(dpy, ReplayPointer, CurrentTime);
            break;
        case KeyPress: {
            KeySym sym = XLookupKeysym(&ev.xkey, 0);
            for (size_t i = 0; i < NELEM(keys); i++) {
                if (sym == keys[i].sym && keys[i].mod == ev.xkey.state) {
                    Arg a  = keys[i].arg;
                    int sel = selclient[curspace];
                    int nc  = nclients[curspace];
                     
                    switch (keys[i].act) {
                    case EXEC:
                        if (!fork()) {
                            if (dpy) close(ConnectionNumber(dpy));
                            setsid();
                            execvp(((char**)a.v)[0], (char**)a.v);
                            _exit(1);
                        }
                        break;
                    case VIEW:
                        if (a.i >= 0 && a.i < NSPACE) {
                            curspace = a.i;
                            tile();
                        }
                        break;
                    case CYCLE:
                        if (nc > 0) {
                            selclient[curspace] = ((sel + a.i) % nc + nc) % nc;
                            tile();
                        }
                        break;
                    case QUIT:
                        running = 0;
                        break;
                    case CLOSE:
                        if (nc) {
                            XKillClient(dpy, clients[curspace][sel].win);
                        }
                        break;
                    case FULLSCR:
                        if (nc) {
                            clients[curspace][sel].isfull ^= 1;
                            tile();
                        }
                        break;
                    case RESIZE:
                        splitratio += a.f;
                        if (splitratio < 0.1f) splitratio = 0.1f;
                        if (splitratio > 0.9f) splitratio = 0.9f;
                        tile();
                        break;
                    case SWAP:
                        if (nc > 1) {
                            int t = ((sel + a.i) % nc + nc) % nc;
                            Client tmp = clients[curspace][sel];
                            clients[curspace][sel] = clients[curspace][t];
                            clients[curspace][t] = tmp;
                            selclient[curspace] = t;
                            tile();
                        }
                        break;
                    case SEND:
                        if (nc && a.i >= 0 && a.i < NSPACE && a.i != curspace && nclients[a.i] < NCLIENT) {
                            Window w = clients[curspace][sel].win;
                            rmclient(w);
                            clients[a.i][nclients[a.i]++] = (Client){ w, 0 };
                            tile();
                        }
                        break;
                    }
                }
            }
            break;
        }
        }
    }
     
    XCloseDisplay(dpy);
    return 0;
}
