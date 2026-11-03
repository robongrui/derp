// DERP: every action the player can bind, in the order of F4's Bindings
// page. BIND(name, title, section, role, key, key2, pad) has three config
// entries: key_<name> and key2_<name> (keyboard keys or mouse buttons) and
// pad_<name> (a gamepad button, bind_keys.h). PRESS(name, title, section,
// key, pad) is a fixed keyboard key that only gets a pad button: pad_<name>
// presses it. role is who acts on it (game_profile.h's GAME_ACTION_*): the
// game's own are GAME, the rest the platform's. Included with BIND and PRESS
// defined by g_game.c (the variables), m_misc.c (the config) and
// platform/games/doom/profile.c (the page).
BIND(up,		"Forward",		"Move",		GAME,		'w',		KEY_UPARROW,	KEY_NONE)
BIND(down,		"Backward",		"Move",		GAME,		's',		KEY_DOWNARROW,	KEY_NONE)
BIND(strafeleft,	"Strafe left",		"Move",		GAME,		'a',		',',		KEY_NONE)
BIND(straferight,	"Strafe right",		"Move",		GAME,		'd',		'.',		KEY_NONE)
BIND(left,		"Turn left",		"Move",		GAME,		KEY_LEFTARROW,	KEY_NONE,	KEY_NONE)
BIND(right,		"Turn right",		"Move",		GAME,		KEY_RIGHTARROW,	KEY_NONE,	KEY_NONE)
BIND(speed,		"Run (hold)",		"Move",		GAME,		KEY_RSHIFT,	KEY_NONE,	KEY_NONE)
BIND(strafe,		"Strafe (hold)",	"Move",		GAME,		KEY_RALT,	KEY_MOUSE1+1,	KEY_NONE)
BIND(jump,		"Jump (Phobos)",	"Move",		GAME,		'e',		KEY_NONE,	KEY_PAD_A)
BIND(boots,		"Grav boots (Phobos)",	"Move",		GAME,		'g',		KEY_NONE,	KEY_NONE)
BIND(fire,		"Fire",			"Act",		GAME,		KEY_RCTRL,	KEY_MOUSE1,	KEY_PADRT)
BIND(use,		"Use",			"Act",		GAME,		' ',		KEY_NONE,	KEY_PAD_X)
BIND(flashlight,	"Flashlight",		"Act",		FLASHLIGHT,	'f',		KEY_NONE,	KEY_PADLT)
BIND(centerview,	"Center view",		"Act",		CENTER_VIEW,	KEY_NONE,	KEY_NONE,	KEY_PAD_R3)
BIND(gyro,		"Gyro aim (hold)",	"Act",		GYRO,		KEY_NONE,	KEY_NONE,	KEY_PAD_R4)
BIND(nextweapon,	"Next weapon",		"Weapons",	GAME,		KEY_WHEELDOWN,	KEY_NONE,	KEY_PAD_RB)
BIND(prevweapon,	"Previous weapon",	"Weapons",	GAME,		KEY_WHEELUP,	KEY_NONE,	KEY_PAD_LB)
BIND(weapon1,		"Fist, chainsaw",	"Weapons",	GAME,		'1',		KEY_NONE,	KEY_NONE)
BIND(weapon2,		"Pistol",		"Weapons",	GAME,		'2',		KEY_NONE,	KEY_NONE)
BIND(weapon3,		"Shotguns",		"Weapons",	GAME,		'3',		KEY_NONE,	KEY_NONE)
BIND(weapon4,		"Chaingun",		"Weapons",	GAME,		'4',		KEY_NONE,	KEY_NONE)
BIND(weapon5,		"Rocket launcher",	"Weapons",	GAME,		'5',		KEY_NONE,	KEY_NONE)
BIND(weapon6,		"Plasma rifle",		"Weapons",	GAME,		'6',		KEY_NONE,	KEY_NONE)
BIND(weapon7,		"BFG 9000",		"Weapons",	GAME,		'7',		KEY_NONE,	KEY_NONE)
BIND(weapon8,		"Chainsaw",		"Weapons",	GAME,		'8',		KEY_NONE,	KEY_NONE)
PRESS(automap,		"Automap",		"Screens",	KEY_TAB,	KEY_PAD_BACK)
PRESS(quicksave,	"Quicksave",		"Screens",	KEY_F6,		KEY_NONE)
PRESS(quickload,	"Quickload",		"Screens",	KEY_F9,		KEY_NONE)
BIND(screenshot,	"Screenshot",		"Screens",	SCREENSHOT,	KEY_F7,		KEY_NONE,	KEY_NONE)
BIND(stats,		"Show FPS step",	"Screens",	STATS,		'`',		KEY_NONE,	KEY_NONE)
BIND(fullscreen,	"Fullscreen",		"Screens",	FULLSCREEN,	KEY_F11,	KEY_NONE,	KEY_NONE)
