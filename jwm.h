#define NSPACE     9
#define NCLIENT    64       /* kept for compatibility; BSP has no hard cap */
#define BARH       24
#define GAP        0        /* px gap around each window; set to e.g. 8 for gaps */

enum { EXEC, VIEW, CYCLE, SWAP, SEND, RESIZE, FULLSCR, CLOSE, QUIT, FLOAT, SPLITDIR, VIEW_ADJ };

typedef union  { int i; float f; const char **v; } Arg;
typedef struct { unsigned int mod; KeySym sym; int act; Arg arg; } Key;

/* modifier keys */
#define ALTKEY  Mod1Mask
#define MODKEY  Mod4Mask
#define SHTKEY  ShiftMask

/* shell commands */
static const char *termcmd[] = { "kitty",   NULL };
static const char *menucmd[] = { "dmenu_run", NULL };
static const char *scrscmd[] = { "/bin/sh", "-c",
    "maim | xclip -selection clipboard -t image/png", NULL };
static const char *browcmd[] = { "firefox", NULL };
static const char *filecmd[] = { "dolphin", NULL };

#define WS(n)                                           \
        { MODKEY,         XK_##n, VIEW, {.i = n-1} },  \
        { MODKEY|SHTKEY,  XK_##n, SEND, {.i = n-1} }

#define VL(k, a) \
        { 0, k, EXEC, {.v = (const char*[]){ "pactl", "set-sink-volume", \
                "@DEFAULT_SINK@", a, NULL }} }

#define BR(k, a) \
        { 0, k, EXEC, {.v = (const char*[]){ "brightnessctl", "set", a, NULL }} }

static Key keys[] = {
        /* launch */
        { MODKEY,           XK_t,      EXEC,     {.v = termcmd}  },
        { MODKEY,           XK_b,      EXEC,     {.v = browcmd}  },
        { MODKEY,           XK_e,      EXEC,     {.v = filecmd}  },
        { MODKEY,           XK_space,  EXEC,     {.v = menucmd}  },
        { MODKEY|SHTKEY,    XK_s,      EXEC,     {.v = scrscmd}  },

        /* focus cycling */
        { MODKEY,           XK_Left,   CYCLE,    {.i = +1}       },
        { MODKEY,           XK_Right,  CYCLE,    {.i = -1}       },

        /* swap windows in tree */
        { MODKEY|SHTKEY,    XK_Left,   SWAP,     {.i = +1}       },
        { MODKEY|SHTKEY,    XK_Right,  SWAP,     {.i = -1}       },

        /* resize (adjust parent split ratio) */
        { MODKEY|ALTKEY,    XK_Left,   RESIZE,   {.f = -0.05f}   },
        { MODKEY|ALTKEY,    XK_Right,  RESIZE,   {.f = +0.05f}   },

        /* toggle split direction of parent node */
        { MODKEY,           XK_v,      SPLITDIR, {0}             },

        /* window state */
        { MODKEY,           XK_f,      FULLSCR,  {0}             },
        { MODKEY,           XK_w,      FLOAT,    {0}             },

        /* wm control */
        { MODKEY,           XK_q,      CLOSE,    {0}             },
        { MODKEY|SHTKEY,    XK_q,      QUIT,     {0}             },

        /* media / brightness */
        VL(XF86XK_AudioRaiseVolume,  "+5%"),
        VL(XF86XK_AudioLowerVolume,  "-5%"),
        BR(XF86XK_MonBrightnessUp,   "+5%"),
        BR(XF86XK_MonBrightnessDown, "5%-"),

        /* workspaces */
        { MODKEY,        XK_Page_Down, VIEW_ADJ, {.i = +1}       },
        { MODKEY,        XK_Page_Up,   VIEW_ADJ, {.i = -1}       },
        WS(1), WS(2), WS(3), WS(4), WS(5), WS(6), WS(7), WS(8), WS(9),
};
