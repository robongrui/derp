/* The biography every reader's book tells (biography.c reads it). Fixed
   text, the reader's facts ($N name, $Y year of birth, $B birthplace,
   $H hometown, $S star sign, $P pet, $F favourite food), <with a pet|without>,
   and wordings {a|b|c|d}: 2, 4, 8 or 16 of them, never nested, none starting
   with a fact, each differing from the others before its first fact and
   before either ends, so a text can only have been written one way.
   \n ends a paragraph, \v is a blank line, \f starts a new page. */
#ifndef BIOGRAPHY_TEMPLATE_H
#define BIOGRAPHY_TEMPLATE_H

/* Before the days, after them, and the days in turn: day d is "one <weekday>
   in <month>, " and template d % BIOGRAPHY_DAY_TEMPLATES, each worth
   BIOGRAPHY_DAY_BITS of wording; the days hold all of the 2048. */
#define BIOGRAPHY_DAYS 128
#define BIOGRAPHY_DAY_BITS 16
#define BIOGRAPHY_DAY_TEMPLATES 16

extern const char *const Biography_Opening;
extern const char *const Biography_Closing;
extern const char *const Biography_Days[BIOGRAPHY_DAY_TEMPLATES];
extern const char *const Biography_Weekdays[7];
extern const char *const Biography_Months[12];

#endif
