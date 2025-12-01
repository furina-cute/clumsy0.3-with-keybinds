#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <Windows.h>
#include <conio.h>
#include "iup.h"
#include "common.h"

// ! the order decides which module get processed first
Module* modules[MODULE_CNT] = {
    &lagModule,
    &dropModule,
    &throttleModule,
    &dupModule,
    &oodModule,
    &tamperModule,
    &resetModule,
	&bandwidthModule,
};

volatile short sendState = SEND_STATUS_NONE;

// global iup handlers
static Ihandle *dialog, *topFrame, *bottomFrame; 
static Ihandle *statusLabel;
static Ihandle *filterText, *filterButton;
Ihandle *filterSelectList;
// timer to update icons
static Ihandle *stateIcon;
static Ihandle *timer;
static Ihandle *timeout = NULL;

void showStatus(const char *line);
static int KEYPRESS_CB(Ihandle *ih, int c, int press);
static int uiOnDialogShow(Ihandle *ih, int state);
static int uiStopCb(Ihandle *ih);
static int uiStartCb(Ihandle *ih);
static int uiTimerCb(Ihandle *ih);
static int uiTimeoutCb(Ihandle *ih);
static int uiListSelectCb(Ihandle *ih, char *text, int item, int state);
static int uiFilterTextCb(Ihandle *ih);
static void uiSetupModule(Module *module, Ihandle *parent);

// serializing config files using a stupid custom format
#define CONFIG_FILE "config.txt"
#define CONFIG_MAX_RECORDS 64
#define CONFIG_BUF_SIZE 4096
typedef struct {
    char* filterName;
    char* filterValue;
} filterRecord;
UINT filtersSize;
filterRecord filters[CONFIG_MAX_RECORDS] = {0};
char configBuf[CONFIG_BUF_SIZE+2]; // add some padding to write \n
BOOL parameterized = 0; // parameterized flag, means reading args from command line

// hotkey configurations
int startKey = 116; // Default: F5
int stopKey = 117;  // Default: F6
int startKeyModifiers = 0; // 0=none, 1=ctrl, 2=shift, 4=alt (can be combined)
int stopKeyModifiers = 0;

// Mapping table for key names to virtual key codes
typedef struct {
    const char* name;
    int vkCode;
} KeyMapping;

static const KeyMapping keyMap[] = {
    // Function keys
    {"F1", 112}, {"F2", 113}, {"F3", 114}, {"F4", 115},
    {"F5", 116}, {"F6", 117}, {"F7", 118}, {"F8", 119},
    {"F9", 120}, {"F10", 121}, {"F11", 122}, {"F12", 123},
    
    // Letter keys
    {"A", 0x41}, {"B", 0x42}, {"C", 0x43}, {"D", 0x44}, {"E", 0x45},
    {"F", 0x46}, {"G", 0x47}, {"H", 0x48}, {"I", 0x49}, {"J", 0x4A},
    {"K", 0x4B}, {"L", 0x4C}, {"M", 0x4D}, {"N", 0x4E}, {"O", 0x4F},
    {"P", 0x50}, {"Q", 0x51}, {"R", 0x52}, {"S", 0x53}, {"T", 0x54},
    {"U", 0x55}, {"V", 0x56}, {"W", 0x57}, {"X", 0x58}, {"Y", 0x59},
    {"Z", 0x5A},
    
    // Number keys
    {"0", 0x30}, {"1", 0x31}, {"2", 0x32}, {"3", 0x33}, {"4", 0x34},
    {"5", 0x35}, {"6", 0x36}, {"7", 0x37}, {"8", 0x38}, {"9", 0x39},
    
    // Special keys
    {"ESC", 0x1B}, {"ESCAPE", 0x1B},
    {"TAB", 0x09},
    {"ENTER", 0x0D}, {"RETURN", 0x0D},
    {"SPACE", 0x20},
    {"BACKSPACE", 0x08}, {"BACK", 0x08},
    {"DELETE", 0x2E}, {"DEL", 0x2E},
    {"INSERT", 0x2D}, {"INS", 0x2D},
    {"HOME", 0x24},
    {"END", 0x23},
    {"PAGEUP", 0x21}, {"PAGE_UP", 0x21},
    {"PAGEDOWN", 0x22}, {"PAGE_DOWN", 0x22},
    {"LEFT", 0x25}, {"LEFTARROW", 0x25},
    {"RIGHT", 0x27}, {"RIGHTARROW", 0x27},
    {"UP", 0x26}, {"UPARROW", 0x26},
    {"DOWN", 0x28}, {"DOWNARROW", 0x28},
    {"PRINTSCREEN", 0x2C}, {"PRINT", 0x2C},
    {"PAUSE", 0x13},
    {"NUMLOCK", 0x90}, {"NUM_LOCK", 0x90},
    {"CAPSLOCK", 0x14}, {"CAPS_LOCK", 0x14},
    {"SCROLLLOCK", 0x91}, {"SCROLL_LOCK", 0x91},
    
    // Keypad numbers
    {"NUMPAD0", 0x60}, {"NUMPAD_0", 0x60},
    {"NUMPAD1", 0x61}, {"NUMPAD_1", 0x61},
    {"NUMPAD2", 0x62}, {"NUMPAD_2", 0x62},
    {"NUMPAD3", 0x63}, {"NUMPAD_3", 0x63},
    {"NUMPAD4", 0x64}, {"NUMPAD_4", 0x64},
    {"NUMPAD5", 0x65}, {"NUMPAD_5", 0x65},
    {"NUMPAD6", 0x66}, {"NUMPAD_6", 0x66},
    {"NUMPAD7", 0x67}, {"NUMPAD_7", 0x67},
    {"NUMPAD8", 0x68}, {"NUMPAD_8", 0x68},
    {"NUMPAD9", 0x69}, {"NUMPAD_9", 0x69},
    
    // Keypad operations
    {"NUMPAD_ADD", 0x6B}, {"NUMPAD_PLUS", 0x6B},
    {"NUMPAD_SUBTRACT", 0x6D}, {"NUMPAD_MINUS", 0x6D},
    {"NUMPAD_MULTIPLY", 0x6A}, {"NUMPAD_STAR", 0x6A},
    {"NUMPAD_DIVIDE", 0x6F}, {"NUMPAD_SLASH", 0x6F},
    {"NUMPAD_DECIMAL", 0x6E}, {"NUMPAD_DOT", 0x6E},
    
    // Windows VK_* constants
    {"VK_LBUTTON", 0x01},
    {"VK_RBUTTON", 0x02},
    {"VK_CANCEL", 0x03},
    {"VK_MBUTTON", 0x04},
    {"VK_XBUTTON1", 0x05},
    {"VK_XBUTTON2", 0x06},
    {"VK_BACK", 0x08},
    {"VK_TAB", 0x09},
    {"VK_CLEAR", 0x0C},
    {"VK_RETURN", 0x0D},
    {"VK_SHIFT", 0x10},
    {"VK_CONTROL", 0x11},
    {"VK_MENU", 0x12},
    {"VK_PAUSE", 0x13},
    {"VK_CAPITAL", 0x14},
    {"VK_KANA", 0x15},
    {"VK_HANGEUL", 0x15},
    {"VK_HANGUL", 0x15},
    {"VK_IME_ON", 0x16},
    {"VK_JUNJA", 0x17},
    {"VK_FINAL", 0x18},
    {"VK_HANJA", 0x19},
    {"VK_KANJI", 0x19},
    {"VK_IME_OFF", 0x1A},
    {"VK_ESCAPE", 0x1B},
    {"VK_CONVERT", 0x1C},
    {"VK_NONCONVERT", 0x1D},
    {"VK_ACCEPT", 0x1E},
    {"VK_MODECHANGE", 0x1F},
    {"VK_SPACE", 0x20},
    {"VK_PRIOR", 0x21},
    {"VK_NEXT", 0x22},
    {"VK_END", 0x23},
    {"VK_HOME", 0x24},
    {"VK_LEFT", 0x25},
    {"VK_UP", 0x26},
    {"VK_RIGHT", 0x27},
    {"VK_DOWN", 0x28},
    {"VK_SELECT", 0x29},
    {"VK_PRINT", 0x2A},
    {"VK_EXECUTE", 0x2B},
    {"VK_SNAPSHOT", 0x2C},
    {"VK_INSERT", 0x2D},
    {"VK_DELETE", 0x2E},
    {"VK_HELP", 0x2F},
    {"VK_LWIN", 0x5B},
    {"VK_RWIN", 0x5C},
    {"VK_APPS", 0x5D},
    {"VK_SLEEP", 0x5F},
    {"VK_NUMPAD0", 0x60},
    {"VK_NUMPAD1", 0x61},
    {"VK_NUMPAD2", 0x62},
    {"VK_NUMPAD3", 0x63},
    {"VK_NUMPAD4", 0x64},
    {"VK_NUMPAD5", 0x65},
    {"VK_NUMPAD6", 0x66},
    {"VK_NUMPAD7", 0x67},
    {"VK_NUMPAD8", 0x68},
    {"VK_NUMPAD9", 0x69},
    {"VK_MULTIPLY", 0x6A},
    {"VK_ADD", 0x6B},
    {"VK_SEPARATOR", 0x6C},
    {"VK_SUBTRACT", 0x6D},
    {"VK_DECIMAL", 0x6E},
    {"VK_DIVIDE", 0x6F},
    {"VK_F1", 0x70},
    {"VK_F2", 0x71},
    {"VK_F3", 0x72},
    {"VK_F4", 0x73},
    {"VK_F5", 0x74},
    {"VK_F6", 0x75},
    {"VK_F7", 0x76},
    {"VK_F8", 0x77},
    {"VK_F9", 0x78},
    {"VK_F10", 0x79},
    {"VK_F11", 0x7A},
    {"VK_F12", 0x7B},
    {"VK_F13", 0x7C},
    {"VK_F14", 0x7D},
    {"VK_F15", 0x7E},
    {"VK_F16", 0x7F},
    {"VK_F17", 0x80},
    {"VK_F18", 0x81},
    {"VK_F19", 0x82},
    {"VK_F20", 0x83},
    {"VK_F21", 0x84},
    {"VK_F22", 0x85},
    {"VK_F23", 0x86},
    {"VK_F24", 0x87},
    {"VK_NUMLOCK", 0x90},
    {"VK_SCROLL", 0x91},
    {"VK_LSHIFT", 0xA0},
    {"VK_RSHIFT", 0xA1},
    {"VK_LCONTROL", 0xA2},
    {"VK_RCONTROL", 0xA3},
    {"VK_LMENU", 0xA4},
    {"VK_RMENU", 0xA5},
    {"VK_BROWSER_BACK", 0xA6},
    {"VK_BROWSER_FORWARD", 0xA7},
    {"VK_BROWSER_REFRESH", 0xA8},
    {"VK_BROWSER_STOP", 0xA9},
    {"VK_BROWSER_SEARCH", 0xAA},
    {"VK_BROWSER_FAVORITES", 0xAB},
    {"VK_BROWSER_HOME", 0xAC},
    {"VK_VOLUME_MUTE", 0xAD},
    {"VK_VOLUME_DOWN", 0xAE},
    {"VK_VOLUME_UP", 0xAF},
    {"VK_MEDIA_NEXT_TRACK", 0xB0},
    {"VK_MEDIA_PREV_TRACK", 0xB1},
    {"VK_MEDIA_STOP", 0xB2},
    {"VK_MEDIA_PLAY_PAUSE", 0xB3},
    {"VK_LAUNCH_MAIL", 0xB4},
    {"VK_LAUNCH_MEDIA_SELECT", 0xB5},
    {"VK_LAUNCH_APP1", 0xB6},
    {"VK_LAUNCH_APP2", 0xB7},
    {"VK_OEM_1", 0xBA},
    {"VK_OEM_PLUS", 0xBB},
    {"VK_OEM_COMMA", 0xBC},
    {"VK_OEM_MINUS", 0xBD},
    {"VK_OEM_PERIOD", 0xBE},
    {"VK_OEM_2", 0xBF},
    {"VK_OEM_3", 0xC0},
    {"VK_OEM_4", 0xDB},
    {"VK_OEM_5", 0xDC},
    {"VK_OEM_6", 0xDD},
    {"VK_OEM_7", 0xDE},
    {"VK_OEM_8", 0xDF},
    {"VK_OEM_102", 0xE2},
    {"VK_PROCESSKEY", 0xE5},
    {"VK_PACKET", 0xE7},
    {"VK_ATTN", 0xF6},
    {"VK_CRSEL", 0xF7},
    {"VK_EXSEL", 0xF8},
    {"VK_EREOF", 0xF9},
    {"VK_PLAY", 0xFA},
    {"VK_ZOOM", 0xFB},
    {"VK_NONAME", 0xFC},
    {"VK_PA1", 0xFD},
    {"VK_OEM_CLEAR", 0xFE},

    {NULL, 0} // Terminator
};

// Convert key name to virtual key code (case-insensitive)
// Supports: key names (F5, Tab, A), VK_* constants (VK_F5, VK_TAB), and hex codes (0xA5, 0x70)
static int keyNameToVkCode(const char* keyName) {
    if (!keyName) return 0;
    
    char upperName[64];
    int i = 0;
    
    // Convert to uppercase
    while (i < sizeof(upperName) - 1 && keyName[i]) {
        upperName[i] = (char)toupper((unsigned char)keyName[i]);
        i++;
    }
    upperName[i] = '\0';
    
    // Trim leading/trailing whitespace
    char* start = upperName;
    while (*start && isspace(*start)) start++;
    char* end = start + strlen(start) - 1;
    while (end >= start && isspace(*end)) {
        *end = '\0';
        end--;
    }
    
    // Search in key map
    for (int j = 0; keyMap[j].name != NULL; j++) {
        if (strcmp(start, keyMap[j].name) == 0) {
            return keyMap[j].vkCode;
        }
    }
    
    // Try parsing as hexadecimal (0xA5, 0xFF, etc.)
    if ((start[0] == '0' || start[0] == '0') && (start[1] == 'x' || start[1] == 'X')) {
        return (int)strtol(start, NULL, 16);
    }
    
    // If not found in map, try parsing as decimal number
    return atoi(start);
}

// loading up filters and fill in
// Parse hotkey format like "ctrl+F5" or "shift+alt+F6"
// Supports both text names (F5, Enter, A) and numeric codes (116, 13, 65)
// Returns: key code (vkCode), modifiers stored in out parameter
void parseHotkey(const char* hotkeyStr, int* outVkCode, int* outModifiers) {
    char buf[64];
    char* token;
    char* next;
    int modifiers = 0;
    int vkCode = 0;
    
    strncpy(buf, hotkeyStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    // Parse tokens separated by '+'
    token = buf;
    do {
        next = strchr(token, '+');
        if (next) *next = '\0';
        
        // Trim whitespace
        while (isspace(*token)) token++;
        char* end = token + strlen(token) - 1;
        while (end >= token && isspace(*end)) {
            *end = '\0';
            end--;
        }
        
        if (strlen(token) == 0) {
            if (next) token = next + 1;
            else break;
            continue;
        }
        
        if (strcmp(token, "ctrl") == 0) {
            modifiers |= 1;
        } else if (strcmp(token, "shift") == 0) {
            modifiers |= 2;
        } else if (strcmp(token, "alt") == 0) {
            modifiers |= 4;
        } else {
            // Try to convert key name to virtual key code
            vkCode = keyNameToVkCode(token);
        }
        
        if (next) token = next + 1;
        else break;
    } while (token && *token);
    
    *outVkCode = vkCode;
    *outModifiers = modifiers;
}

void loadConfig() {
    char path[MSG_BUFSIZE];
    char *p;
    FILE *f;
    GetModuleFileName(NULL, path, MSG_BUFSIZE);
    LOG("Executable path: %s", path);
    p = strrchr(path, '\\');
    if (p == NULL) p = strrchr(path, '/'); // holy shit
    strcpy(p+1, CONFIG_FILE);
    LOG("Config path: %s", path);
    f = fopen(path, "r");
    if (f) {
        size_t len;
        char *current, *last;
        len = fread(configBuf, sizeof(char), CONFIG_BUF_SIZE, f);
        if (len == CONFIG_BUF_SIZE) {
            LOG("Config file is larger than %d bytes, get truncated.", CONFIG_BUF_SIZE);
        }
        // always patch in a newline at the end to ease parsing
        configBuf[len] = '\n';
        configBuf[len+1] = '\0';

        // parse out the kv pairs. isn't quite safe
        filtersSize = 0;
        last = current = configBuf;
        do {
            // eat up empty lines
EAT_SPACE:  while (isspace(*current)) { ++current; }
            if (*current == '#') {
                current = strchr(current, '\n');
                if (!current) break;
                current = current + 1;
                goto EAT_SPACE;
            }

            // now we can start
            last = current;
            current = strchr(last, ':');
            if (!current) break;
            *current = '\0';
            
            // parse hotkey configurations
            if (strcmp(last, "start_key") == 0) {
                current += 1;
                while (isspace(*current)) { ++current; }
                last = current;
                current = strchr(last, '\n');
                if (!current) break;
                *current = '\0';
                parseHotkey(last, &startKey, &startKeyModifiers);
                last = current = current + 1;
                continue;
            } else if (strcmp(last, "stop_key") == 0) {
                current += 1;
                while (isspace(*current)) { ++current; }
                last = current;
                current = strchr(last, '\n');
                if (!current) break;
                *current = '\0';
                parseHotkey(last, &stopKey, &stopKeyModifiers);
                last = current = current + 1;
                continue;
            }
            
            filters[filtersSize].filterName = last;
            current += 1;
            while (isspace(*current)) { ++current; } // eat potential space after :
            last = current;
            current = strchr(last, '\n');
            if (!current) break;
            filters[filtersSize].filterValue = last;
            *current = '\0';
            if (*(current-1) == '\r') *(current-1) = 0;
            last = current = current + 1;
            ++filtersSize;
        } while (last && last - configBuf < CONFIG_BUF_SIZE);
        LOG("Loaded %u records.", filtersSize);
    }

    if (!f || filtersSize == 0)
    {
        LOG("Failed to load from config. Fill in a simple one.");
        // config is missing or ill-formed. fill in some simple ones
        filters[filtersSize].filterName = "loopback packets";
        filters[filtersSize].filterValue = "outbound and ip.DstAddr >= 127.0.0.1 and ip.DstAddr <= 127.255.255.255";
        filtersSize = 1;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc( int nCode, WPARAM wParam, LPARAM lParam )
{
   char pressedKey;
   // Declare a pointer to the KBDLLHOOKSTRUCTdsad
   KBDLLHOOKSTRUCT *pKeyBoard = (KBDLLHOOKSTRUCT *)lParam;
   switch( wParam )
   {
       case WM_KEYUP: // When the key has been pressed and released
       {
          //get the key code
          pressedKey = (char)pKeyBoard->vkCode;
       }
       break;
       default:
           return CallNextHookEx( NULL, nCode, wParam, lParam );
       break;
   }

    // Get current modifier key states
    int currentModifiers = 0;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
        currentModifiers |= 1; // ctrl
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        currentModifiers |= 2; // shift
    }
    if (GetAsyncKeyState(VK_MENU) & 0x8000) {
        currentModifiers |= 4; // alt
    }

    // Check if pressed key matches start or stop key with correct modifiers
    if (pressedKey == startKey && currentModifiers == startKeyModifiers)
    {
        uiStartCb(NULL);
    } else if (pressedKey == stopKey && currentModifiers == stopKeyModifiers)
    {
        uiStopCb(NULL);
    }
    
    // Log key press information (will output if show_console is enabled)
    LOG("Key pressed: VK Code=%d (0x%02X), Modifiers=%d%s%s%s", 
        pressedKey, pressedKey, currentModifiers,
        (currentModifiers & 1) ? " [Ctrl]" : "",
        (currentModifiers & 2) ? " [Shift]" : "",
        (currentModifiers & 4) ? " [Alt]" : "");

   //according to winapi all functions which implement a hook must return by calling next hook
   return CallNextHookEx( NULL, nCode, wParam, lParam);
}

void init(int argc, char* argv[]) {
    UINT ix;
    Ihandle *topVbox, *bottomVbox, *dialogVBox, *controlHbox;
    Ihandle *noneIcon, *doingIcon, *errorIcon;
    char* arg_value = NULL;

    // fill in config
    loadConfig();
    
    // No debug console in non-debug builds: show_console feature removed

    // iup inits
    IupOpen(&argc, &argv);

    // this is so easy to get wrong so it's pretty worth noting in the program
    statusLabel = IupLabel("NOTICE: When capturing localhost (loopback) packets, you CAN'T include inbound criteria.\n"
        "Filters like 'udp' need to be 'udp and outbound' to work. See readme for more info.");
    IupSetAttribute(statusLabel, "EXPAND", "HORIZONTAL");
    IupSetAttribute(statusLabel, "PADDING", "8x8");

    topFrame = IupFrame(
        topVbox = IupVbox(
            filterText = IupText(NULL),
            controlHbox = IupHbox(
                stateIcon = IupLabel(NULL),
                filterButton = IupButton("Start", NULL),
                IupFill(),
                IupLabel("Presets:  "),
                filterSelectList = IupList(NULL),
                NULL
            ),
            NULL
        )
    );

    // parse arguments and set globals *before* setting up UI.
    // arguments can be read and set after callbacks are setup
    // FIXME as Release is built as WindowedApp, stdout/stderr won't show
    LOG("argc: %d", argc);
    if (argc > 1) {
        if (!parseArgs(argc, argv)) {
            fprintf(stderr, "invalid argument count. ensure you're using options as \"--drop on\"");
            exit(-1); // fail fast.
        }
        parameterized = 1;
    }

    IupSetAttribute(topFrame, "TITLE", "Filtering");
    IupSetAttribute(topFrame, "EXPAND", "HORIZONTAL");
    IupSetAttribute(filterText, "EXPAND", "HORIZONTAL");
    IupSetCallback(filterText, "VALUECHANGED_CB", (Icallback)uiFilterTextCb);
    IupSetAttribute(filterButton, "PADDING", "8x");
    IupSetCallback(filterButton, "ACTION", uiStartCb);
    IupSetAttribute(topVbox, "NCMARGIN", "4x4");
    IupSetAttribute(topVbox, "NCGAP", "4x2");
    IupSetAttribute(controlHbox, "ALIGNMENT", "ACENTER");

    // setup state icon
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");
    IupSetAttribute(stateIcon, "PADDING", "4x");

    // fill in options and setup callback
    IupSetAttribute(filterSelectList, "VISIBLECOLUMNS", "24");
    IupSetAttribute(filterSelectList, "DROPDOWN", "YES");
    for (ix = 0; ix < filtersSize; ++ix) {
        char ixBuf[4];
        sprintf(ixBuf, "%d", ix+1); // ! staring from 1, following lua indexing
        IupStoreAttribute(filterSelectList, ixBuf, filters[ix].filterName);
    }
    IupSetAttribute(filterSelectList, "VALUE", "1");
    IupSetCallback(filterSelectList, "ACTION", (Icallback)uiListSelectCb);
    // set filter text value since the callback won't take effect before main loop starts
    IupSetAttribute(filterText, "VALUE", filters[0].filterValue);

    // functionalities frame 
    bottomFrame = IupFrame(
        bottomVbox = IupVbox(
            NULL
        )
    );
    IupSetAttribute(bottomFrame, "TITLE", "Functions");
    IupSetAttribute(bottomVbox, "NCMARGIN", "4x4");
    IupSetAttribute(bottomVbox, "NCGAP", "4x2");

    // create icons
    noneIcon = IupImage(8, 8, icon8x8);
    doingIcon = IupImage(8, 8, icon8x8);
    errorIcon = IupImage(8, 8, icon8x8);
    IupSetAttribute(noneIcon, "0", "BGCOLOR");
    IupSetAttribute(noneIcon, "1", "224 224 224");
    IupSetAttribute(doingIcon, "0", "BGCOLOR");
    IupSetAttribute(doingIcon, "1", "109 170 44");
    IupSetAttribute(errorIcon, "0", "BGCOLOR");
    IupSetAttribute(errorIcon, "1", "208 70 72");
    IupSetHandle("none_icon", noneIcon);
    IupSetHandle("doing_icon", doingIcon);
    IupSetHandle("error_icon", errorIcon);

    // setup module uis
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        uiSetupModule(*(modules+ix), bottomVbox);
    }

    // dialog
    dialog = IupDialog(
        dialogVBox = IupVbox(
            topFrame,
            bottomFrame,
            statusLabel,
            NULL
        )
    );

    IupSetAttribute(dialog, "TITLE", "clumsy " CLUMSY_VERSION);
    IupSetAttribute(dialog, "SIZE", "480x"); // add padding manually to width
    IupSetAttribute(dialog, "RESIZE", "NO");
    IupSetCallback(dialog, "SHOW_CB", (Icallback)uiOnDialogShow);


    // global layout settings to affect childrens
    IupSetAttribute(dialogVBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(dialogVBox, "NCMARGIN", "4x4");
    IupSetAttribute(dialogVBox, "NCGAP", "4x2");

    // setup timer
    timer = IupTimer();
    IupSetAttribute(timer, "TIME", STR(ICON_UPDATE_MS));
    IupSetCallback(timer, "ACTION_CB", uiTimerCb);

    // setup timeout of program
    arg_value = IupGetGlobal("timeout");
    if(arg_value != NULL)
    {
        char valueBuf[16];
        sprintf(valueBuf, "%s000", arg_value);  // convert from seconds to milliseconds

        timeout = IupTimer();
        IupStoreAttribute(timeout, "TIME", valueBuf);
        IupSetCallback(timeout, "ACTION_CB", uiTimeoutCb);
        IupSetAttribute(timeout, "RUN", "YES");
    }

     //Retrieve the applications instance
    HINSTANCE instance = GetModuleHandle(NULL);
    //Set a global Windows Hook to capture keystrokes using the function declared above
    HHOOK test1 = SetWindowsHookEx( WH_KEYBOARD_LL, LowLevelKeyboardProc, instance,0);
}

void startup() {
    // initialize seed
    srand((unsigned int)time(NULL));

    // kickoff event loops
    IupShowXY(dialog, IUP_CENTER, IUP_CENTER);
    IupMainLoop();
    // ! main loop won't return until program exit
}

void cleanup() {

    IupDestroy(timer);
    if (timeout) {
        IupDestroy(timeout);
    }

    IupClose();
    endTimePeriod(); // try close if not closing
}

// ui logics
void showStatus(const char *line) {
    IupStoreAttribute(statusLabel, "TITLE", line); 
}

static int KEYPRESS_CB(Ihandle *ih, int c, int press){
    LOG("Character: %d",c);
}

// in fact only 32bit binary would run on 64 bit os
// if this happens pop out message box and exit
static BOOL check32RunningOn64(HWND hWnd) {
    BOOL is64ret;
    // consider IsWow64Process return value
    if (IsWow64Process(GetCurrentProcess(), &is64ret) && is64ret) {
        MessageBox(hWnd, (LPCSTR)"You're running 32bit clumsy on 64bit Windows, which wouldn't work. Please use the 64bit clumsy version.",
            (LPCSTR)"Aborting", MB_OK);
        return TRUE;
    }
    return FALSE;
}

static BOOL checkIsRunning() {
    //It will be closed and destroyed when programm terminates (according to MSDN).
    HANDLE hStartEvent = CreateEventW(NULL, FALSE, FALSE, L"Global\\CLUMSY_IS_RUNNING_EVENT_NAME");

    if (hStartEvent == NULL)
        return TRUE;

    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hStartEvent);
        hStartEvent = NULL;
        return TRUE;
    }

    return FALSE;
}


static int uiOnDialogShow(Ihandle *ih, int state) {
    // only need to process on show
    HWND hWnd;
    BOOL exit;
    HICON icon;
    HINSTANCE hInstance;
    if (state != IUP_SHOW) return IUP_DEFAULT;
    hWnd = (HWND)IupGetAttribute(ih, "HWND");
    hInstance = GetModuleHandle(NULL);

    // set application icon
    icon = LoadIcon(hInstance, "CLUMSY_ICON");
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);

    exit = checkIsRunning();
    if (exit) {
        MessageBox(hWnd, (LPCSTR)"Theres' already an instance of clumsy running.",
            (LPCSTR)"Aborting", MB_OK);
        return IUP_CLOSE;
    }

#ifdef _WIN32
    exit = check32RunningOn64(hWnd);
    if (exit) {
        return IUP_CLOSE;
    }
#endif

    // try elevate and decides whether to exit
    exit = tryElevate(hWnd, parameterized);

    if (!exit && parameterized) {
        setFromParameter(filterText, "VALUE", "filter");
        LOG("is parameterized, start filtering upon execution.");
        uiStartCb(filterButton);
    }

    return exit ? IUP_CLOSE : IUP_DEFAULT;
}

static int uiStartCb(Ihandle *ih) {
    char buf[MSG_BUFSIZE];
    if(ih)
    {
        UNREFERENCED_PARAMETER(ih);
    }
    if (divertStart(IupGetAttribute(filterText, "VALUE"), buf) == 0) {
        showStatus(buf);
        return IUP_DEFAULT;
    }

    // successfully started
    showStatus("Started filtering. Enable functionalities to take effect.");
    IupSetAttribute(filterText, "ACTIVE", "NO");
    IupSetAttribute(filterButton, "TITLE", "Stop");
    IupSetCallback(filterButton, "ACTION", uiStopCb);
    IupSetAttribute(timer, "RUN", "YES");

    return IUP_DEFAULT;
}

static int uiStopCb(Ihandle *ih) {
    int ix;
    if(ih)
    {
        UNREFERENCED_PARAMETER(ih);
    }
    
    // try stopping
    IupSetAttribute(filterButton, "ACTIVE", "NO");
    IupFlush(); // flush to show disabled state
    divertStop();

    IupSetAttribute(filterText, "ACTIVE", "YES");
    IupSetAttribute(filterButton, "TITLE", "Start");
    IupSetAttribute(filterButton, "ACTIVE", "YES");
    IupSetCallback(filterButton, "ACTION", uiStartCb);

    // stop timer and clean up icons
    IupSetAttribute(timer, "RUN", "NO");
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        modules[ix]->processTriggered = 0; // use = here since is threads already stopped
        IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
    }
    sendState = SEND_STATUS_NONE;
    IupSetAttribute(stateIcon, "IMAGE", "none_icon");

    showStatus("Stopped. To begin again, edit criteria and click Start.");
    return IUP_DEFAULT;
}

static int uiToggleControls(Ihandle *ih, int state) {
    Ihandle *controls = (Ihandle*)IupGetAttribute(ih, CONTROLS_HANDLE);
    short *target = (short*)IupGetAttribute(ih, SYNCED_VALUE);
    int controlsActive = IupGetInt(controls, "ACTIVE");
    if (controlsActive && !state) {
        IupSetAttribute(controls, "ACTIVE", "NO");
        InterlockedExchange16(target, I2S(state));
    } else if (!controlsActive && state) {
        IupSetAttribute(controls, "ACTIVE", "YES");
        InterlockedExchange16(target, I2S(state));
    }

    return IUP_DEFAULT;
}

static int uiTimerCb(Ihandle *ih) {
    int ix;
    UNREFERENCED_PARAMETER(ih);
    for (ix = 0; ix < MODULE_CNT; ++ix) {
        if (modules[ix]->processTriggered) {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "doing_icon");
            InterlockedAnd16(&(modules[ix]->processTriggered), 0);
        } else {
            IupSetAttribute(modules[ix]->iconHandle, "IMAGE", "none_icon");
        }
    }

    // update global send status icon
    switch (sendState)
    {
    case SEND_STATUS_NONE:
        IupSetAttribute(stateIcon, "IMAGE", "none_icon");
        break;
    case SEND_STATUS_SEND:
        IupSetAttribute(stateIcon, "IMAGE", "doing_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    case SEND_STATUS_FAIL:
        IupSetAttribute(stateIcon, "IMAGE", "error_icon");
        InterlockedAnd16(&sendState, SEND_STATUS_NONE);
        break;
    }

    return IUP_DEFAULT;
}

static int uiTimeoutCb(Ihandle *ih) {
    UNREFERENCED_PARAMETER(ih);
    return IUP_CLOSE;
 }

static int uiListSelectCb(Ihandle *ih, char *text, int item, int state) {
    UNREFERENCED_PARAMETER(text);
    UNREFERENCED_PARAMETER(ih);
    if (state == 1) {
        IupSetAttribute(filterText, "VALUE", filters[item-1].filterValue);
    }
    return IUP_DEFAULT;
}

static int uiFilterTextCb(Ihandle *ih)  {
    UNREFERENCED_PARAMETER(ih);
    // unselect list
    IupSetAttribute(filterSelectList, "VALUE", "0");
    return IUP_DEFAULT;
}

static void uiSetupModule(Module *module, Ihandle *parent) {
    Ihandle *groupBox, *toggle, *controls, *icon;
    groupBox = IupHbox(
        icon = IupLabel(NULL),
        toggle = IupToggle(module->displayName, NULL),
        IupFill(),
        controls = module->setupUIFunc(),
        NULL
    );
    IupSetAttribute(groupBox, "EXPAND", "HORIZONTAL");
    IupSetAttribute(groupBox, "ALIGNMENT", "ACENTER");
    IupSetAttribute(controls, "ALIGNMENT", "ACENTER");
    IupAppend(parent, groupBox);

    // set controls as attribute to toggle and enable toggle callback
    IupSetCallback(toggle, "ACTION", (Icallback)uiToggleControls);
    IupSetAttribute(toggle, CONTROLS_HANDLE, (char*)controls);
    IupSetAttribute(toggle, SYNCED_VALUE, (char*)module->enabledFlag);
    IupSetAttribute(controls, "ACTIVE", "NO"); // startup as inactive
    IupSetAttribute(controls, "NCGAP", "4"); // startup as inactive

    // set default icon
    IupSetAttribute(icon, "IMAGE", "none_icon");
    IupSetAttribute(icon, "PADDING", "4x");
    module->iconHandle = icon;

    // parameterize toggle
    if (parameterized) {
        setFromParameter(toggle, "VALUE", module->shortName);
    }
}

int main(int argc, char* argv[]) {
    LOG("Is Run As Admin: %d", IsRunAsAdmin());
    LOG("Is Elevated: %d", IsElevated());
    init(argc, argv);
    startup();
    cleanup();
    return 0;
}
