#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>
#include <intuition/gadgetclass.h>
#include <libraries/gadtools.h>
#include <clib/graphics_protos.h>
#include <clib/intuition_protos.h>
#include <clib/gadtools_protos.h>

struct Gadget *
createAllGadgets(struct Gadget **glistptr, void *vi,UWORD topborder,  struct Gadget *my_gads[]);

/* Gadget defines of our choosing, to be used as GadgetID's,
** also used as the index into the gadget array my_gads[].
*/
#define MYGAD_STRING1   (0)
#define MYGAD_STRING2   (1)
#define MYGAD_STRING3   (2)
#define MYGAD_BUTTON    (3)

struct TextAttr Topaz80 = { "topaz.font", 8, 0, 0, };

#define MY_WIN_LEFT   (20)
#define MY_WIN_TOP    (10)
#define MY_WIN_WIDTH  (500)
#define MY_WIN_HEIGHT (110)

typedef struct {
  struct Gadget* list;
  struct Gadget* connectedFighters;
  struct Gadget* dashboardConnections;
} amiga_gadgets_t;

struct Library *IntuitionBase = 0;
struct Library *GfxBase = 0;
struct Library *GadToolsBase = 0;
amiga_gadgets_t gadgets = {0};

void
amiga_cleanupGUI(void)
{
  if (global.window) {
    CloseWindow(global.window);
  }

  if (gadgets.list) {
    FreeGadgets(gadgets.list);
  }

  if (IntuitionBase) {
    CloseLibrary(IntuitionBase);
  }
  if (GfxBase) {
    CloseLibrary(GfxBase);
  }
  if (GadToolsBase) {
    CloseLibrary(GadToolsBase);
  }
}

void
amiga_openWindow(void)
{
  IntuitionBase = OpenLibrary("intuition.library",37);
  GfxBase = OpenLibrary("graphics.library", 37);
  GadToolsBase = OpenLibrary("gadtools.library", 37);

  if (!GfxBase || !GadToolsBase || !IntuitionBase) {
    log_printf("amiga_openWindow: failed to open libraries\n");
    network_exit(1);
  }

  void            *vi;
  struct Screen   *mysc;
  struct Gadget   *my_gads[4];

  if ((mysc = LockPubScreen(NULL)) == NULL || (vi = GetVisualInfo(mysc, TAG_END)) == NULL) {
    log_printf("amiga_openWindow: failed to get screen info\n");
    network_exit(1);
  }

  UWORD           topborder;

  topborder = mysc->WBorTop + (mysc->Font->ta_YSize + 1);

  if (createAllGadgets(&gadgets.list, vi, topborder, my_gads) == NULL) {
    log_printf("amiga_openWindow: failed to create gadgets\n");
    network_exit(1);
  }

  struct TagItem win_tags[] =  {
    {WA_Gadgets,     (int)gadgets.list},
    {WA_Left,        MY_WIN_LEFT},
    {WA_Top,         MY_WIN_TOP},
    {WA_Width,       MY_WIN_WIDTH},
    {WA_Height,      MY_WIN_HEIGHT},
    {WA_Activate,    TRUE},
    {WA_DepthGadget, TRUE},
    {WA_DragBar,     TRUE},
    {WA_CloseGadget, TRUE},
    {WA_SimpleRefresh, TRUE},
    {WA_IDCMP,       IDCMP_CLOSEWINDOW|IDCMP_REFRESHWINDOW/*|STRINGIDCMP|WFLG_ACTIVATE*/},
    {WA_NewLookMenus, TRUE},
    {WA_Title,       (int)"Roidserver"},
    {TAG_DONE,       (int)NULL},
  };

  if ((global.window = OpenWindowTagList(NULL,win_tags)) == NULL) {
    log_printf("amiga_openWindow: failed to open window\n");
    network_exit(1);
  }

  GT_RefreshWindow (global.window, NULL);
}

/*
** handleIDCMP() - handle all of the messages from an IDCMP.
*/
BOOL handleIDCMP(struct Window *win, BOOL done)
{
  struct IntuiMessage *message;
  USHORT code;
  SHORT mousex, mousey;
  ULONG class;

  /* Remove all of the messages from the port by calling GetMsg()
  ** until it returns NULL.
  **
  ** The code should be able to handle three cases:
  **
  ** 1.  No messages waiting at the port, and the first call to GetMsg()
  ** returns NULL.  In this case the code should do nothing.
  **
  ** 2.  A single message waiting.  The code should remove the message,
  ** processes it, and finish.
  **
  ** 3.  Multiple messages waiting.  The code should process each waiting
  ** message, and finish.
  */
  while (NULL != (message = (struct IntuiMessage *)GetMsg(win->UserPort)))  {
    /* It is often convenient to copy the data out of the message.
    ** In many cases, this lets the application reply to the message
    ** quickly.  Copying the data is not required, if the code does
    ** not reply to the message until the end of the loop, then
    ** it may directly reference the message information anywhere
    ** before the reply.
    */
    class  = message->Class;
    code   = message->Code;
    mousex = message->MouseX;
    mousey = message->MouseY;

    /* The loop should reply as soon as possible.  Note that the code
    ** may not reference data in the message after replying to the
    ** message.  Thus, the application should not reply to the message
    ** until it is done referencing information in it.
    **
    ** Be sure to reply to every message received with GetMsg().
    */
    ReplyMsg((struct Message *)message);

    /* The class contains the IDCMP type of the message. */
    switch (class) {
    case IDCMP_CLOSEWINDOW:
      done = TRUE;
      break;
    case IDCMP_VANILLAKEY:
      printf("IDCMP_VANILLAKEY (%lc)\n",code);
      break;
    case IDCMP_RAWKEY:
      printf("IDCMP_RAWKEY\n");
      break;
    case IDCMP_DISKINSERTED:
      printf("IDCMP_DISKINSERTED\n");
      break;
    case IDCMP_DISKREMOVED:
      printf("IDCMP_DISKREMOVED\n");
      break;
    case IDCMP_REFRESHWINDOW:
      /* With GadTools, the application must use GT_BeginRefresh()
      ** where it would normally have used BeginRefresh()
      */
      GT_BeginRefresh(global.window);
      GT_EndRefresh(global.window, TRUE);
      break;
    case IDCMP_MOUSEBUTTONS:
      /* the code often contains useful data, such as the ASCII
      ** value (for IDCMP_VANILLAKEY), or the type of button
      ** event here.
      */
      switch (code)
	{
	case SELECTUP:
	  printf("SELECTUP at %d,%d\n",mousex,mousey);
	  break;
	case SELECTDOWN:
	  printf("SELECTDOWN at %d,%d\n",mousex,mousey);
	  break;
	case MENUUP:
	  printf("MENUUP\n");
	  break;
	case MENUDOWN:
	  printf("MENUDOWN\n");
	  break;
	default:
	  printf("UNKNOWN CODE\n");
	  break;
	}
      break;
    default:
      printf("Unknown IDCMP message\n");
      break;
    }
  }
  return(done);
}


struct Gadget*
amiga_createGadgets(struct Gadget **glistptr, void *vi, UWORD topborder, struct Gadget *my_gads[])
{
  struct NewGadget ng;
  struct Gadget *gad;

  gad = CreateContext(glistptr);

  ng.ng_LeftEdge   = 240;
  ng.ng_TopEdge    = 10+topborder;
  ng.ng_Width      = 100;
  ng.ng_TextAttr   = &Topaz80;
  ng.ng_VisualInfo = vi;
  ng.ng_Flags      = NG_HIGHLABEL;

  ng.ng_Height     = 14;
  ng.ng_GadgetText = "Connected Fighters:";
  ng.ng_GadgetID   = MYGAD_STRING1;
  gadgets.connectedFighters = gad = CreateGadget(NUMBER_KIND, gad, &ng,
					      GTNM_Number, 0,
					      GTNM_Border, TRUE,
					      TAG_END);

  ng.ng_TopEdge   += 20;
  ng.ng_GadgetText = "Dashboard Connections: ";
  ng.ng_GadgetID   = MYGAD_STRING2;
  gadgets.dashboardConnections = gad = CreateGadget(NUMBER_KIND, gad, &ng,
					      GTNM_Number, 0,
					      GTNM_Border, TRUE,
					      TAG_END);

  return(gad);
}


void
amiga_updateNumConnectedFighters(int num)
{
  log_printf("amiga_updateNumConnectedFighters: %d\n", num);
  GT_SetGadgetAttrs(gadgets.connectedFighters, global.window, NULL,
		    GTNM_Number, num,
		    TAG_END);
  //  GT_RefreshWindow (global.window, NULL);

}
