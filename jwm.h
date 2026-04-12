#define NSPACE     9
#define NCLIENT    7
#define BARH       0

enum { EXEC, VIEW, CYCLE, SWAP, SEND, RESIZE, FULLSCR, CLOSE, QUIT };

typedef union  { int i; float f; const char **v; } Arg;
typedef struct { unsigned int mod; KeySym sym; int act; Arg arg; } Key;

// modifier keys
#define ALTKEY  Mod1Mask
#define MODKEY  Mod4Mask
#define SHTKEY  ShiftMask

// shell commands
static const char *termcmd[] = { "kitty", NULL };
static const char *menucmd[] = { "dmenu_run", NULL };
static const char *scrscmd[] = { "/bin/sh", "-c", "maim | xclip -selection clipboard -t image/png", NULL };
static const char *browcmd[] = { "firefox", NULL };
static const char *filecmd[] = { "dolphin", NULL };

#define WS(n)                                      \
        { MODKEY, XK_##n, VIEW, {.i=n-1} },        \
        { MODKEY|SHTKEY, XK_##n, SEND, {.i=n-1} }

#define VL(k, a)                                   \
        { 0, k, EXEC, {.v = (const char*[]){ "pactl", "set-sink-volume", "@DEFAULT_SINK@", a, NULL }} }

#define BR(k, a)                                   \
        { 0, k, EXEC, {.v = (const char*[]){ "brightnessctl", "set", a, NULL }} }

static Key keys[] = {
        { MODKEY,           XK_t,      EXEC,    {.v = termcmd}  },
        { MODKEY,           XK_b,      EXEC,    {.v = browcmd}  },
        { MODKEY,           XK_e,      EXEC,    {.v = filecmd}  },
        { MODKEY,           XK_space,  EXEC,    {.v = menucmd}  },
        { MODKEY|SHTKEY,    XK_s,      EXEC,    {.v = scrscmd}  },
        { MODKEY,           XK_Left,   CYCLE,   {.i = +1}       },
        { MODKEY,           XK_Right,  CYCLE,   {.i = -1}       },
        { MODKEY|SHTKEY,    XK_Left,   SWAP,    {.i = +1}       },
        { MODKEY|SHTKEY,    XK_Right,  SWAP,    {.i = -1}       },
        { MODKEY|ALTKEY,    XK_Left,   RESIZE,  {.f = -0.05f}   },
        { MODKEY|ALTKEY,    XK_Right,  RESIZE,  {.f = +0.05f}   },
        { MODKEY,           XK_q,      CLOSE,   {0}             },
        { MODKEY|SHTKEY,    XK_q,      QUIT,    {0}             },
        { MODKEY,           XK_f,      FULLSCR, {0}             },
        VL(XF86XK_AudioRaiseVolume, "+5%"),
        VL(XF86XK_AudioLowerVolume, "-5%"),
        BR(XF86XK_MonBrightnessUp, "+5%"),
        BR(XF86XK_MonBrightnessDown, "5%-"),
        WS(1), WS(2), WS(3), WS(4), WS(5), WS(6), WS(7), WS(8), WS(9),
};
