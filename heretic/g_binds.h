// DERP: every action the player can bind, in the order of F4's Bindings
// page. BIND(name, title, section, role, key, key2, pad) has three config
// entries: key_<name> and key2_<name> (keyboard keys or mouse buttons) and
// pad_<name> (a gamepad button, bind_keys.h). PRESS(name, title, section,
// key, pad) is a fixed keyboard key that only gets a pad button: pad_<name>
// presses it. role is who acts on it (game_profile.h's GAME_ACTION_*): the
// game's own are GAME, the rest the platform's. Included with BIND and PRESS
// defined by g_game.c (the variables), m_misc.c (the config) and
// platform/games/heretic/profile.c (the page).
BIND(up,		"Forward",		"Move",		GAME,		'w',		KEY_UPARROW,	KEY_NONE)
BIND(down,		"Backward",		"Move",		GAME,		's',		KEY_DOWNARROW,	KEY_NONE)
BIND(strafeleft,	"Strafe left",		"Move",		GAME,		'a',		',',		KEY_NONE)
BIND(straferight,	"Strafe right",		"Move",		GAME,		'd',		'.',		KEY_NONE)
BIND(left,		"Turn left",		"Move",		GAME,		KEY_LEFTARROW,	KEY_NONE,	KEY_NONE)
BIND(right,		"Turn right",		"Move",		GAME,		KEY_RIGHTARROW,	KEY_NONE,	KEY_NONE)
BIND(speed,		"Run (hold)",		"Move",		GAME,		KEY_RSHIFT,	KEY_NONE,	KEY_NONE)
BIND(strafe,		"Strafe (hold)",	"Move",		GAME,		KEY_RALT,	KEY_MOUSE1+1,	KEY_NONE)
BIND(flyup,		"Fly up",		"Move",		GAME,		'u',		KEY_NONE,	KEY_PAD_A)
BIND(flydown,		"Fly down",		"Move",		GAME,		'j',		KEY_NONE,	KEY_PAD_B)
BIND(flycenter,		"Stop flying",		"Move",		GAME,		'k',		KEY_NONE,	KEY_NONE)
BIND(lookup,		"Look up",		"Move",		GAME,		'm',		KEY_NONE,	KEY_NONE)
BIND(lookdown,		"Look down",		"Move",		GAME,		'b',		KEY_NONE,	KEY_NONE)
BIND(lookcenter,	"Center view",		"Move",		GAME,		'n',		KEY_NONE,	KEY_PAD_R3)
BIND(fire,		"Fire",			"Act",		GAME,		KEY_RCTRL,	KEY_MOUSE1,	KEY_PADRT)
BIND(use,		"Use",			"Act",		GAME,		' ',		KEY_NONE,	KEY_PAD_X)
BIND(useartifact,	"Use artifact",		"Act",		GAME,		KEY_ENTER,	KEY_NONE,	KEY_PADLT)
BIND(invleft,		"Inventory left",	"Act",		GAME,		'[',		KEY_NONE,	KEY_PAD_LEFT)
BIND(invright,		"Inventory right",	"Act",		GAME,		']',		KEY_NONE,	KEY_PAD_RIGHT)
BIND(tome,		"Tome of Power",	"Act",		GAME,		KEY_BACKSPACE,	KEY_NONE,	KEY_PAD_Y)
BIND(gyro,		"Gyro aim (hold)",	"Act",		GYRO,		KEY_NONE,	KEY_NONE,	KEY_PAD_R4)
BIND(nextweapon,	"Next weapon",		"Weapons",	GAME,		KEY_WHEELDOWN,	KEY_NONE,	KEY_PAD_RB)
BIND(prevweapon,	"Previous weapon",	"Weapons",	GAME,		KEY_WHEELUP,	KEY_NONE,	KEY_PAD_LB)
BIND(weapon1,		"Staff, gauntlets",	"Weapons",	GAME,		'1',		KEY_NONE,	KEY_NONE)
BIND(weapon2,		"Elven wand",		"Weapons",	GAME,		'2',		KEY_NONE,	KEY_NONE)
BIND(weapon3,		"Ethereal crossbow",	"Weapons",	GAME,		'3',		KEY_NONE,	KEY_NONE)
BIND(weapon4,		"Dragon claw",		"Weapons",	GAME,		'4',		KEY_NONE,	KEY_NONE)
BIND(weapon5,		"Hellstaff",		"Weapons",	GAME,		'5',		KEY_NONE,	KEY_NONE)
BIND(weapon6,		"Phoenix rod",		"Weapons",	GAME,		'6',		KEY_NONE,	KEY_NONE)
BIND(weapon7,		"Firemace",		"Weapons",	GAME,		'7',		KEY_NONE,	KEY_NONE)
PRESS(automap,		"Automap",		"Screens",	KEY_TAB,	KEY_PAD_BACK)
PRESS(quicksave,	"Quicksave",		"Screens",	KEY_F6,		KEY_NONE)
PRESS(quickload,	"Quickload",		"Screens",	KEY_F9,		KEY_NONE)
BIND(screenshot,	"Screenshot",		"Screens",	SCREENSHOT,	KEY_F7,		KEY_NONE,	KEY_NONE)
BIND(stats,		"Show FPS step",	"Screens",	STATS,		'`',		KEY_NONE,	KEY_NONE)
BIND(fullscreen,	"Fullscreen",		"Screens",	FULLSCREEN,	KEY_F11,	KEY_NONE,	KEY_NONE)
