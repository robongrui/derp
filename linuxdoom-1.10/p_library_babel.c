//
// What the library's people say (Game_LibrarySays): about a third of the
// indifferent ones, each in a language of its own, says something now and
// then, five seconds at a time, and the renderer writes it over its head,
// its letters coming and going a few at a time, as a mumble does.
// Babel's tower, a little: eleven languages, Latin and Ancient Greek among
// them (people have been here a long time), in the game's font, so without
// their accents and the Greek in Latin letters. Drawing only: no thinker,
// no random number; who says what and when follows from the person's
// place, its floor and the clock.
// (See p_library.h, p_library_local.h.)
//

#include <math.h>
#include <stdint.h>

#include "doomdef.h"
#include "doomstat.h"
#include "p_library_local.h"

#define LANGUAGES	11
#define LINES		14
#define EVERY		25.0	// seconds from one thing said to the next
#define SAYING		5.0	// and how long each is said
#define COMING		0.5	// its letters coming, over this at first
#define GOING		1.5	// and going, over this at the end

static const char* const said[LANGUAGES][LINES] =
{
    {	// English
	"HELLO.",
	"ARE YOU LOOKING FOR YOUR BOOK TOO?",
	"I HAVE BEEN HERE SO LONG.",
	"NONE OF THEM ARE MINE.",
	"IT IS ALL NONSENSE.",
	"THERE IS NO BOTTOM.",
	"I FORGOT MY OWN NAME.",
	"I THINK I HAVE READ THIS ONE BEFORE.",
	"DO YOU REMEMBER THE SUN?",
	"I USED TO COUNT THE FLOORS.",
	"SOMEONE JUMPED YESTERDAY.",
	"HE WAS BACK THIS MORNING.",
	"PLEASE DON'T MOVE THAT BOOK.",
	"MAYBE THE NEXT ONE IS MINE.",
    },
    {	// German
	"HALLO.",
	"SUCHST DU AUCH DEIN BUCH?",
	"ICH BIN SCHON SO LANGE HIER.",
	"KEINES DAVON IST MEINS.",
	"ES IST ALLES UNSINN.",
	"ES GIBT KEINEN BODEN.",
	"ICH HABE MEINEN NAMEN VERGESSEN.",
	"ICH GLAUBE, DAS HABE ICH SCHON GELESEN.",
	"ERINNERST DU DICH NOCH AN DIE SONNE?",
	"FRUEHER HABE ICH DIE STOCKWERKE GEZAEHLT.",
	"GESTERN IST JEMAND GESPRUNGEN.",
	"HEUTE MORGEN WAR ER WIEDER DA.",
	"BITTE LASS DAS BUCH DA STEHEN.",
	"VIELLEICHT IST DAS NAECHSTE MEINS.",
    },
    {	// French
	"BONJOUR.",
	"TU CHERCHES AUSSI TON LIVRE?",
	"JE SUIS ICI DEPUIS SI LONGTEMPS.",
	"AUCUN N'EST LE MIEN.",
	"CE N'EST QUE DU CHARABIA.",
	"IL N'Y A PAS DE FOND.",
	"J'AI OUBLIE MON NOM.",
	"JE CROIS AVOIR DEJA LU CELUI-CI.",
	"TU TE SOUVIENS DU SOLEIL?",
	"AVANT, JE COMPTAIS LES ETAGES.",
	"QUELQU'UN A SAUTE HIER.",
	"IL ETAIT DE RETOUR CE MATIN.",
	"S'IL TE PLAIT, LAISSE CE LIVRE A SA PLACE.",
	"LE PROCHAIN SERA PEUT-ETRE LE MIEN.",
    },
    {	// Spanish
	"HOLA.",
	"TU TAMBIEN BUSCAS TU LIBRO?",
	"LLEVO TANTO TIEMPO AQUI.",
	"NINGUNO ES EL MIO.",
	"TODO ES UN SINSENTIDO.",
	"NO HAY FONDO.",
	"OLVIDE MI PROPIO NOMBRE.",
	"CREO QUE YA HE LEIDO ESTE.",
	"RECUERDAS EL SOL?",
	"ANTES CONTABA LOS PISOS.",
	"AYER ALGUIEN SALTO.",
	"ESTA MANANA YA HABIA VUELTO.",
	"POR FAVOR, DEJA ESE LIBRO EN SU SITIO.",
	"QUIZA EL SIGUIENTE SEA EL MIO.",
    },
    {	// Italian
	"CIAO.",
	"ANCHE TU CERCHI IL TUO LIBRO?",
	"SONO QUI DA COSI TANTO TEMPO.",
	"NESSUNO DI QUESTI E IL MIO.",
	"E TUTTO SENZA SENSO.",
	"NON C'E UN FONDO.",
	"HO DIMENTICATO IL MIO NOME.",
	"CREDO DI AVER GIA LETTO QUESTO.",
	"TI RICORDI IL SOLE?",
	"PRIMA CONTAVO I PIANI.",
	"IERI QUALCUNO SI E BUTTATO.",
	"STAMATTINA ERA DI NUOVO QUI.",
	"PER FAVORE, LASCIA QUEL LIBRO AL SUO POSTO.",
	"FORSE IL PROSSIMO E IL MIO.",
    },
    {	// Portuguese
	"OLA.",
	"TAMBEM PROCURAS O TEU LIVRO?",
	"ESTOU AQUI HA TANTO TEMPO.",
	"NENHUM DELES E O MEU.",
	"E TUDO SEM SENTIDO.",
	"NAO HA FUNDO.",
	"ESQUECI O MEU NOME.",
	"ACHO QUE JA LI ESTE.",
	"AINDA TE LEMBRAS DO SOL?",
	"ANTES CONTAVA OS ANDARES.",
	"ONTEM ALGUEM SALTOU.",
	"ESTA MANHA JA ESTAVA DE VOLTA.",
	"POR FAVOR, DEIXA ESSE LIVRO NO SEU LUGAR.",
	"TALVEZ O PROXIMO SEJA O MEU.",
    },
    {	// Dutch
	"HALLO.",
	"ZOEK JIJ OOK JE BOEK?",
	"IK BEN HIER AL ZO LANG.",
	"GEEN ENKELE IS VAN MIJ.",
	"HET IS ALLEMAAL ONZIN.",
	"ER IS GEEN BODEM.",
	"IK BEN MIJN NAAM VERGETEN.",
	"IK DENK DAT IK DEZE AL GELEZEN HEB.",
	"WEET JE NOG HOE DE ZON ERUITZIET?",
	"VROEGER TELDE IK DE VERDIEPINGEN.",
	"GISTEREN IS ER IEMAND GESPRONGEN.",
	"VANOCHTEND WAS HIJ ER WEER.",
	"LAAT DAT BOEK ALSJEBLIEFT STAAN.",
	"MISSCHIEN IS DE VOLGENDE VAN MIJ.",
    },
    {	// Polish
	"CZESC.",
	"TEZ SZUKASZ SWOJEJ KSIAZKI?",
	"JESTEM TU OD TAK DAWNA.",
	"ZADNA Z NICH NIE JEST MOJA.",
	"TO WSZYSTKO BEZ SENSU.",
	"TU NIE MA DNA.",
	"ZAPOMNIALEM, JAK SIE NAZYWAM.",
	"CHYBA JUZ TO CZYTALEM.",
	"PAMIETASZ JESZCZE SLONCE?",
	"KIEDYS LICZYLEM PIETRA.",
	"WCZORAJ KTOS SKOCZYL.",
	"RANO ZNOWU TU BYL.",
	"PROSZE, ZOSTAW TA KSIAZKE NA MIEJSCU.",
	"MOZE NASTEPNA BEDZIE MOJA.",
    },
    {	// Swedish
	"HEJ.",
	"LETAR DU OCKSA EFTER DIN BOK?",
	"JAG HAR VARIT HAR SA LANGE.",
	"INGEN AV DEM AR MIN.",
	"ALLT AR NONSENS.",
	"DET FINNS INGEN BOTTEN.",
	"JAG HAR GLOMT MITT NAMN.",
	"JAG TROR ATT JAG REDAN HAR LAST DEN HAR.",
	"MINNS DU SOLEN?",
	"FORR RAKNADE JAG VANINGARNA.",
	"NAGON HOPPADE NER IGAR.",
	"HAN VAR TILLBAKA I MORSE.",
	"LAT DEN DAR BOKEN STA KVAR.",
	"KANSKE AR NASTA MIN.",
    },
    {	// Latin
	"SALVE.",
	"TUNE QUOQUE LIBRUM TUUM QUAERIS?",
	"IAM DIU HIC SUM.",
	"NULLUS EORUM MEUS EST.",
	"OMNIA NUGAE SUNT.",
	"FUNDUS NON EST.",
	"NOMEN MEUM OBLITUS SUM.",
	"PUTO ME HUNC IAM LEGISSE.",
	"MEMINISTINE SOLIS?",
	"OLIM TABULATA NUMERABAM.",
	"HERI ALIQUIS DESILUIT.",
	"HODIE MANE REDIIT.",
	"QUAESO, LIBRUM ILLUM SUO LOCO RELINQUE.",
	"FORTASSE PROXIMUS MEUS ERIT.",
    },
    {	// Ancient Greek, in Latin letters
	"CHAIRE.",
	"ARA KAI SY TEN SEAUTOU BIBLON ZETEIS?",
	"POLYN DE CHRONON ENTHADE EIMI.",
	"OUDEMIA TOUTON EME ESTIN.",
	"PANTA PHLYARIA.",
	"OUK ESTI PYTHMEN.",
	"TOU EMAUTOU ONOMATOS EPELATHOMEN.",
	"DOKO TAUTEN ANEGNOKENAI EDE.",
	"MEMNESAI TOU HELIOU?",
	"PROTERON ERITHMOUN TAS STEGAS.",
	"CHTHES TIS EXEPEDESEN.",
	"HEOTHEN PALIN PAREN.",
	"ME KINEI TAUTEN TEN BIBLON.",
	"ISOS HE HEPOMENE EME.",
    },
};


static uint32_t Mix (uint32_t h)
{
    h ^= h >> 16;
    h *= 0x7feb352d;
    h ^= h >> 15;
    h *= 0x846ca68b;
    h ^= h >> 16;
    return h;
}


const char* P_LibrarySays (const mobj_t* mo, double seconds, float* shown)
{
    int		place, slot;
    uint32_t	who;
    double	at;

    if (!P_Library () || !P_LibraryIndifferent ((mobj_t*)mo) || !L_PersonOf (mo, &place, &slot))
	return NULL;
    who = Mix ((uint32_t)place*2654435761u ^ (uint32_t)slot*40503u ^ (uint32_t)L_Climbed ()*97u);
    if (who % 3)
	return NULL;			// two in three keep to themselves
    at = fmod (seconds + (who >> 8) % 1000*0.1, EVERY);
    if (at >= SAYING)
	return NULL;
    *shown = at < COMING ? (float)(at/COMING) : at > SAYING - GOING ? (float)((SAYING - at)/GOING) : 1;
    return said[(who >> 4) % LANGUAGES]
	[Mix (who ^ (uint32_t)((seconds + (who >> 8) % 1000*0.1)/EVERY)) % LINES];
}
