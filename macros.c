// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Ashhar Farhan
//
// macros.c — the function keys' macros, read from a file.
//
// This is zbitxv2's macros.c, with its names and its way of reading a
// macro. A macro file has a line for each function key:
//
//     F1 CQ, cq cq cq de {MYCALL} {MYCALL} {MYCALL} ar k
//
// the key, the label for its button, a comma, and the text to send. A line
// that does not begin with F is passed over, so # begins a comment. In the
// text a name in braces is put in from the radio's controls as the macro
// is sent:
//
//     {MYCALL}       the station's callsign, the control MYCALLSIGN
//     {CALL}         the other station's, the control CALL
//     {SENTRST}      the report sent, the control SENT
//     {SENTRSTCUT}   5NN
//     {EXCH}         the exchange, the control NR
//     {GRID}         the station's grid, the control MYGRID
//     {GRIDSQUARE}   its first four characters
//     {RUN}, {S&P}   put nothing in; they say whether the station is
//                    running or searching, for macro_label()
//
// and for short, * is {MYCALL}, ! is {CALL} and # is {EXCH}. A control
// that the radio does not have puts nothing in: CALL, SENT and NR are the
// logger's, which is not written yet.
//
// What is changed from zbitxv2: the file is data/<name>.mc, beside the
// radio's other files, and not in the web folder; the values come from the
// control table and not from the window's fields; macro_exec() is told how
// much room there is for the text, which zbitxv2 did not check; and what
// zbitxv2 did for {WIPE} and {SAVE}, which work the logger, and for the
// window's buttons and the web page, is left out until there is something
// for it to work.
//
// A mode may have macros of its own: JTTY has data/jtty.mc. Each file is
// read into a set of its own, struct macro_set, with macro_set_load(), and
// the macro_set_ functions work on the set they are given. zbitxv2's
// functions, macro_load(), macro_labels(), macro_key() and macro_exec(),
// work on the set CW uses, as they always did.
//
// For the thread that owns the controls.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "macros.h"

#define MACRO_PATH_MAX 200
#define MACRO_LINE_MAX 255
#define MACRO_VAR_MAX 20
#define LABELS_BETWEEN '/'
#define NO_KEY (-1)

static struct macro_set cw_set;
static char is_running = 0;

static const char *field_str(const char *name){
	const char *value = control_get(name);

	if (value == NULL)
		return "";
	return value;
}

void macro_label(int fn_key, char *label){
	const struct macro *macro_table = cw_set.table;

	*label = 0;

	for (int i = 0; i < MACRO_MAX; i++){
		if (macro_table[i].fn_key == fn_key){
			strcpy(label, macro_table[i].label);
			if (is_running)
				break;
		}
	}
}

//the labels in the order of the file, with a / between them, for a control's
//choices; a / in a label is shown as a -
void macro_set_labels(const struct macro_set *set, char *labels, int room){
	const struct macro *macro_table = set->table;
	int used = 0;

	labels[0] = 0;
	for (int i = 0; i < MACRO_MAX && macro_table[i].label[0] != 0; i++){
		int length = strlen(macro_table[i].label);

		if (used + length + 2 > room)
			break;
		if (used > 0)
			labels[used++] = LABELS_BETWEEN;
		for (int j = 0; j < length; j++){
			char c = macro_table[i].label[j];
			if (c == LABELS_BETWEEN)
				c = '-';
			labels[used++] = c;
		}
		labels[used] = 0;
	}
}

//the function key of the first macro with a label, as macro_labels() gave it
int macro_set_key(const struct macro_set *set, const char *label){
	const struct macro *macro_table = set->table;
	char shown[MACRO_MAX_LABEL];

	for (int i = 0; i < MACRO_MAX && macro_table[i].label[0] != 0; i++){
		strcpy(shown, macro_table[i].label);
		for (char *p = shown; *p; p++)
			if (*p == LABELS_BETWEEN)
				*p = '-';
		if (!strcmp(shown, label))
			return macro_table[i].fn_key;
	}
	return NO_KEY;
}

int macro_set_load(struct macro_set *set, const char *filename){
	struct macro *macro_table = set->table;
	char macro_line[MACRO_LINE_MAX];
	char full_path[MACRO_PATH_MAX];

	snprintf(full_path, sizeof(full_path), "data/%s.mc", filename);
	FILE *pf = fopen(full_path, "r");

	if(!pf)
		return -1;

	memset(set->table, 0, sizeof(set->table));
	int i = 0;
	while (i < MACRO_MAX){
		if (fgets(macro_line, sizeof(macro_line) - 1, pf) == NULL)
			break;

		char *p = macro_line;
		if (*p++ != 'F')
			continue;
		macro_table[i].fn_key = atoi(p);

		//now, skip to beyond the key identifier
		while(*p != ' ' && *p)
			p++;

		//skip the following spaces to the start of button label
		while(*p == ' ' && *p)
			p++;

		int j = 0;
		for (j = 0; j < MACRO_MAX_LABEL - 1; j++){
			if (*p  && *p != ',')
				macro_table[i].label[j] = *p++;
			else
				break;
		}
		macro_table[i].label[j] = 0;

		//expect a comma before the macro text
		if (*p != ','){
			macro_table[i].label[0] = 0;
			continue;
		}

		snprintf(macro_table[i].text, MACRO_MAX_TEXT, "%s", ++p);
		i++;
	}
	fclose(pf);
	return 0;
}

static void macro_get_var(const char *var, char *s, int room){
	*s = 0;

	if(!strcmp(var, "MYCALL"))
		snprintf(s, room, "%s", field_str("MYCALLSIGN"));
	else if (!strcmp(var, "CALL"))
		snprintf(s, room, "%s", field_str("CALL"));
	else if (!strcmp(var, "SENTRST"))
		snprintf(s, room, "%s", field_str("SENT"));
	else if (!strcmp(var, "GRID"))
		snprintf(s, room, "%s", field_str("MYGRID"));
	else if (!strcmp(var, "GRIDSQUARE")){
		snprintf(s, room, "%s", field_str("MYGRID"));
		if (room > 4)
			s[4] = 0;
	}
	else if (!strcmp(var, "EXCH")){
		snprintf(s, room, "%s", field_str("NR"));
	}
}

static char *macro_expand_var(const char *var, char *s, int room){
	*s = 0;

	if (!strcmp(var, "RUN"))
		is_running = 1;
	else if (!strcmp(var, "S&P"))
		is_running = 0;
	else if(!strcmp(var, "MYCALL"))
		macro_get_var(var, s, room);
	else if (!strcmp(var, "CALL"))
		macro_get_var(var, s, room);
	else if (!strcmp(var, "SENTRSTCUT"))
		snprintf(s, room, "5NN");
	else if (!strcmp(var, "SENTRST"))
		macro_get_var(var, s, room);
	else if (!strcmp(var, "EXCH") || !strcmp(var, "EXCHANGE"))
		macro_get_var("EXCH", s, room);
	else if (!strcmp(var, "GRID"))
		macro_get_var(var, s, room);
	else if (!strcmp(var, "GRIDSQUARE"))
		macro_get_var(var, s, room);
	return s + strlen(s);
}

static char *macro_expand_short_var(char v, char *dest, int room){
	if(v == '*')
		return macro_expand_var("MYCALL", dest, room);
	else if (v == '!')
		return macro_expand_var("CALL", dest, room);
	else if (v == '#')
		return macro_expand_var("EXCH", dest, room);
	else if (v == '@')
		return macro_expand_var("FREQ", dest, room);
	return dest;
}

int macro_set_exec(const struct macro_set *set, int key, char *dest, int room){
	const struct macro *macro_table = set->table;
	const struct macro *m = NULL;

	int i;
	for(i = 0; i < MACRO_MAX; i++)
		if (macro_table[i].fn_key == key && macro_table[i].label[0] != 0){
			m = macro_table + i;
			break;
		}

	if(!m || room < 1)
		return -1;

	char *q = dest;
	char *end = dest + room - 1;
	const char *p = m->text;
	char var[MACRO_VAR_MAX];
	char in_var = 0;

	var[0] = 0;
	*q = 0;
	//we end at the string end, newline, etc.
	while (*p >= ' ' && q < end){
		//the sequency of if-else-if is imp.
		if (*p == '!' || *p == '*' || *p == '@' || *p == '#'){
			q = macro_expand_short_var(*p++, q, end - q + 1);
		}
		else if (*p == '}'){
			q = macro_expand_var(var, q, end - q + 1);
			var[0] = 0;
			p++;
			in_var = 0;
		}
		else if (in_var == 1){
			int l = strlen(var);
			if (l < MACRO_VAR_MAX - 1){
				var[l++] = *p;
				var[l] = 0;
			}
			p++;
		}
		else if (*p == '{'){
			in_var = 1;
			p++;
		}
		else{
			*q++ = *p++;
			*q = 0;
		}
	}
	return 0;
}

/* zbitxv2's, for the set CW uses */

void macro_labels(char *labels, int room){
	macro_set_labels(&cw_set, labels, room);
}

int macro_key(const char *label){
	return macro_set_key(&cw_set, label);
}

int macro_load(const char *filename){
	return macro_set_load(&cw_set, filename);
}

int macro_exec(int key, char *dest, int room){
	return macro_set_exec(&cw_set, key, dest, room);
}
