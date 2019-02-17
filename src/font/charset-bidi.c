/*
 *   This file is part of MiniGUI, a mature cross-platform windowing 
 *   and Graphics User Interface (GUI) support system for embedded systems
 *   and smart IoT devices.
 * 
 *   Copyright (C) 2002~2018, Beijing FMSoft Technologies Co., Ltd.
 *   Copyright (C) 1998~2002, WEI Yongming
 * 
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 * 
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 * 
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 * 
 *   Or,
 * 
 *   As this program is a library, any link to this program must follow
 *   GNU General Public License version 3 (GPLv3). If you cannot accept
 *   GPLv3, you need to be licensed from FMSoft.
 * 
 *   If you have got a commercial license of this program, please use it
 *   under the terms and conditions of the commercial license.
 * 
 *   For more information about the commercial license, please refer to
 *   <http://www.minigui.com/en/about/licensing-policy/>.
 */
/*
** charset-bidi.c: The charset Bidirectional Algorithm.
** 
** Created by houhuihua at 2008/01/23
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "common.h"
#include "minigui.h"
#include "gdi.h"

#include "devfont.h"
#include "bidi.h"

#define BIDI_MAX(a,b) ((a) > (b) ? (a) : (b))

/* Some convenience macros */
#define TYPE(list) ((list)->type)
#define LEN(list) ((list)->len)
#define POS(list) ((list)->pos)
#define LEVEL(list) ((list)->level)

/* run item*/
struct _TYPERUN
{
    struct _TYPERUN *prev;
    struct _TYPERUN *next;

    Uint32 type;  /*  char type. */
    int pos, len;       /*  run start position、run len.*/
    BYTE level;         /*  Embedding level. */
};

#ifdef BIDI_DEBUG

static char bidi_type_name(Uint32 c)
{
    switch (c){
        case BIDI_TYPE_LTR: return 'L';
        case BIDI_TYPE_RTL: return 'R';
        case BIDI_TYPE_AL:  return 'A';
        case BIDI_TYPE_EN:  return '1';
        case BIDI_TYPE_AN:  return '9';
        case BIDI_TYPE_ES:  return 'w';
        case BIDI_TYPE_ET:  return 'w';
        case BIDI_TYPE_CS:  return 'w';
        case BIDI_TYPE_NSM: return '`';
        case BIDI_TYPE_BN:  return 'b';
        case BIDI_TYPE_BS:  return 'B';
        case BIDI_TYPE_SS:  return 'S';
        case BIDI_TYPE_WS:  return '_';
        case BIDI_TYPE_ON:  return 'n';
    }
    return 'n';
}

static void print_resolved_levels(TYPERUN *pp)
{
    fprintf(stderr, "   Levels: ");
    while(pp){
        int i;
        for(i = 0; i < LEN (pp); i++)
            fprintf(stderr, "%c", bidi_level[(int)LEVEL(pp)]);
        pp = pp->next;
    }
    fprintf(stderr, "\n");
}

static void print_resolved_types(TYPERUN *pp)
{
    fprintf(stderr, "   Types: ");
    while(pp)
    {
        int i;
        for(i = 0; i < LEN (pp); i++)
            fprintf(stderr, "%c", bidi_type_name(pp->type));
        pp = pp->next;
    }
    fprintf(stderr, "\n");
}

static void print_run_types(TYPERUN *pp)
{
    char level_str[10] = "";
    fprintf(stderr, "   Run types: ");
    while (pp){
        if(pp->level == -1){
            sprintf(level_str, "%s", "-1");
        }
        else sprintf(level_str, "%c", bidi_level[(int)pp->level]);

        fprintf(stderr, "pos:%d:len:%d(%c)[level:%s] || ", pp->pos, pp->len, 
                bidi_type_name(pp->type), level_str);
        pp = pp->next;
    }
    fprintf (stderr, "\n");
} 

static void print_hexstr(Glyph32* str, int len, BOOL reorder_state)
{
    int m = 0;

    DBGLOG("\n====================================================\n");
    if(!reorder_state)
        DBGLOG("Reorder one line start.\n");
    else
        DBGLOG("Reorder one line end.\n");

    DBGLOG("   ");
    for(m = 0; m < len; m++){
        if(m && !(m%16)) DBGLOG("\n   ");
        DBGLOG2("0x%02x ", (unsigned char)str[m]);
    }
    DBGLOG("\n====================================================\n");

}

#else /* BIDI_DEBUG */

inline static void print_hexstr(Glyph32* str, int len, BOOL reorder_state)
{
    return;
}

#endif /* !BIDI_DEBUG */

static void free_typerun_list (TYPERUN *type_rl_list)
{
    TYPERUN *pp;
    if (!type_rl_list){
        return;
    }
    pp = type_rl_list;
    while (pp) {
        TYPERUN *p;
        p = pp;
        pp = pp->next;
        free(p);
    };
}
static TYPERUN* merge_with_prev(TYPERUN *second)
{
    TYPERUN *first = second->prev;
    first->next = second->next;
    first->next->prev = first;
    LEN (first) += LEN (second);
    free(second);
    return first;
}

static void compact_list (TYPERUN *list)
{
    if (list->next){
        for (list = list->next; list; list = list->next){
            if (TYPE (list->prev) == TYPE (list) && LEVEL (list->prev) == LEVEL (list))
                list = merge_with_prev (list);
        }
    }
}

static void compact_neutrals (TYPERUN *list)
{
    if (list->next)
    {
        for (list = list->next; list; list = list->next)
        {
            if (LEVEL (list->prev) == LEVEL (list) &&
                    ((TYPE(list->prev) == TYPE (list)
                      || (BIDI_IS_NEUTRAL (TYPE (list->prev))
                          && BIDI_IS_NEUTRAL (TYPE (list))))))
                list = merge_with_prev (list);
        }
    }
}

#define ADD_TYPE_LINK(p,q) \
    do {	\
        (p)->len  = (q)->pos - (p)->pos;	\
        (p)->next = (q);	\
        (q)->prev = (p);	\
        (p) = (q);	        \
    } while (0)

static TYPERUN* get_runtype_link (const CHARSETOPS* charset_ops, Glyph32* glyphs, int len)
{
    int type = 0;
    TYPERUN *list, *last, *link;
    int i;

    /* Add the starting link */
    list = calloc(1, sizeof(TYPERUN));
    list->type = BIDI_TYPE_SOT;
    list->level = -1;
    last = list;

    for (i = 0; i < len; i++){
        if ((type = charset_ops->bidi_glyph_type (glyphs[i])) != last->type){
            link = calloc(1, sizeof(TYPERUN));
            link->type = type;
            link->pos = i;
            ADD_TYPE_LINK (last, link);
        }
    }

    /* Add the ending link */
    link = calloc(1, sizeof(TYPERUN));
    link->type = BIDI_TYPE_EOT;
    link->level = -1;
    link->pos = len;
    ADD_TYPE_LINK (last, link);

    return list;
}


/* 1.Find base level */
static void bidi_resolveParagraphs(TYPERUN **ptype_rl_list, Uint32* pbase_dir, BYTE* pbase_level)
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;
    DBGLOG("\n1:Finding the base level\n");

    *pbase_dir = BIDI_TYPE_ON;
    for (pp = type_rl_list; pp; pp = pp->next){
        if (BIDI_IS_LETTER(TYPE(pp))){
            *pbase_level = BIDI_DIR_TO_LEVEL(TYPE (pp));
            *pbase_dir = BIDI_LEVEL_TO_DIR(*pbase_level);
            break;
        }
    }
    *pbase_dir = BIDI_LEVEL_TO_DIR (*pbase_level);

    DBGLOG2("   Base level: %c\n", bidi_level[(int)*pbase_level]);
    DBGLOG2("   Base dir: %c\n", bidi_type_name(*pbase_dir));
    DBGLOG("Finding the base level, Done\n");
}

/* 2.Resolving Explicit levels.
 *   now nothing need to do.*/
static void bidi_resolveExplicit (TYPERUN **ptype_rl_list, Uint32 base_dir)
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;
    DBGLOG("\n2:Resolving weak types\n");

    for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
       LEVEL(pp) = BIDI_DIR_TO_LEVEL(base_dir);
    }
    return;
}

/* 3.Resolving weak types */
static void bidi_resolveWeak(TYPERUN **ptype_rl_list, Uint32 base_dir) 
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;
    Uint32 last_strong, prev_type_org;
    BOOL w4;

    DBGLOG("\n3:Resolving weak types\n");
    last_strong = base_dir;

    for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
        Uint32 prev_type, this_type, next_type;

        prev_type = TYPE(pp->prev);
        this_type = TYPE(pp);
        next_type = (pp->next) ? TYPE(pp->next) : this_type;

        if (BIDI_IS_STRONG(prev_type))
            last_strong = prev_type;

        /* W1. NSM
           Examine each non-spacing mark (NSM) in the level run, and change the
           type of the NSM to the type of the previous character. If the NSM
           is at the start of the level run, it will get the type of sor. */
        if (this_type == BIDI_TYPE_NSM)
        {
            if (LEVEL(pp->prev) == LEVEL(pp))
                pp = merge_with_prev(pp);
            else
                TYPE(pp) = prev_type;
        }
        else if (this_type == BIDI_TYPE_EN && last_strong == BIDI_TYPE_AL){  /* W2: European numbers. */
            TYPE (pp) = BIDI_TYPE_AN;
            /* Resolving dependency of loops for rules W1 and W2, so we
               can merge them in one loop. */
            if (next_type == BIDI_TYPE_NSM)
                TYPE(pp->next) = BIDI_TYPE_AN;
        }
    }

    last_strong = base_dir;
    /* Resolving dependency of loops for rules W4 and W5, W5 may
       want to prevent W4 to take effect in the next turn, do this 
       through "w4". */
    w4 = TRUE;
    /* Resolving dependency of loops for rules W4 and W5 with W7,
       W7 may change an EN to L but it sets the prev_type_org if needed,
       so W4 and W5 in next turn can still do their works. */
    prev_type_org = BIDI_TYPE_ON;

    for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
        Uint32 prev_type, this_type, next_type;

        prev_type = TYPE(pp->prev);
        this_type = TYPE(pp);
        next_type = (pp->next) ? TYPE(pp->next) : this_type;

        if (BIDI_IS_STRONG (prev_type))
            last_strong = prev_type;

        /* W3: Change ALs to R. */
        if (this_type == BIDI_TYPE_AL)
        {
            TYPE (pp) = BIDI_TYPE_RTL;
            w4 = TRUE;
            prev_type_org = BIDI_TYPE_ON;
            continue;
        }

        /* W4. A single european separator changes to a european number.
           A single common separator between two numbers of the same type
           changes to that type. */
        if (w4 && LEN (pp) == 1 && BIDI_IS_ES_OR_CS (this_type)
                && BIDI_IS_NUMBER (prev_type_org) && prev_type_org == next_type
                && (prev_type_org == BIDI_TYPE_EN
                    || this_type == BIDI_TYPE_CS))
        {
            TYPE (pp) = prev_type;
            this_type = TYPE (pp);
        }
        w4 = TRUE;

        /* W5. A sequence of European terminators adjacent to European
           numbers changes to All European numbers. */
        if (this_type == BIDI_TYPE_ET
                && (prev_type_org == BIDI_TYPE_EN
                    || next_type == BIDI_TYPE_EN))
        {
            TYPE (pp) = BIDI_TYPE_EN;
            w4 = FALSE;
            this_type = TYPE (pp);
        }

        /* W6. Otherwise change separators and terminators to other neutral. */
        if (BIDI_IS_NUMBER_SEPARATOR_OR_TERMINATOR (this_type))
            TYPE (pp) = BIDI_TYPE_ON;

        /* W7. Change european numbers to L. */
        if (this_type == BIDI_TYPE_EN && last_strong == BIDI_TYPE_LTR)
        {
            TYPE (pp) = BIDI_TYPE_LTR;
            prev_type_org = (LEVEL (pp) == LEVEL (pp->next) ?
                    BIDI_TYPE_EN : BIDI_TYPE_ON);
        }
        else
            prev_type_org = TYPE(pp);
    }

    compact_neutrals (type_rl_list);

#ifdef BIDI_DEBUG
    print_run_types(type_rl_list);
    print_resolved_levels (type_rl_list);
    print_resolved_types (type_rl_list);
#endif

}

/* Return the embedding direction of a link. */
#define BIDI_EMBEDDING_DIR(list) BIDI_LEVEL_TO_DIR(LEVEL(list))

/* 4.Resolving Neutral Types */
static void bidi_resolveNeutrals(TYPERUN **ptype_rl_list, Uint32 base_bir)
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;
    DBGLOG ("\n4:Resolving neutral types\n");
    for (pp = type_rl_list->next; pp->next; pp = pp->next)
    {
        Uint32 prev_type, this_type, next_type;

        prev_type = TYPE(pp->prev);
        this_type = TYPE(pp);
        next_type = (pp->next) ? TYPE(pp->next) : this_type;
        /* "European and arabic numbers are treated as though they were R, NUMBER_TO_RTL does this. */
        this_type = BIDI_NUMBER_TO_RTL(this_type);
        prev_type = BIDI_NUMBER_TO_RTL(prev_type);
        next_type = BIDI_NUMBER_TO_RTL(next_type);

        if (BIDI_IS_NEUTRAL (this_type))
            TYPE (pp) = (prev_type == next_type) ?
                prev_type :                    /* N1. */ 
                BIDI_EMBEDDING_DIR(pp); /* N2. */ 
    }
    compact_list (type_rl_list);
#ifdef BIDI_DEBUG
    print_run_types(type_rl_list);
    print_resolved_levels (type_rl_list);
    print_resolved_types (type_rl_list);
#endif
}

/* 5.Resolving implicit levels. */
static int bidi_resolveImplicit(TYPERUN **ptype_rl_list, int base_level)
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;
    int max_level = base_level;
    DBGLOG ("\n5:Resolving implicit levels\n");

    for (pp = type_rl_list->next; pp->next; pp = pp->next){
        Uint32 this_type;
        int level;

        this_type = TYPE(pp);
        level = LEVEL(pp);

        /* I1. Even */
        /* I2. Odd */
        if (BIDI_IS_NUMBER(this_type))
            LEVEL(pp) = (level + 2) & ~1;
        else
            LEVEL(pp) = (level ^ BIDI_DIR_TO_LEVEL(this_type)) + (level & 1);

        if (LEVEL(pp) > max_level)
            max_level = LEVEL(pp);
    }
    compact_list (type_rl_list);
#ifdef BIDI_DEBUG
    print_run_types(type_rl_list);
    print_resolved_levels (type_rl_list);
    print_resolved_types (type_rl_list);
#endif
    return max_level;
}

/* 6.Resolving Mirrored Char. */
static void 
bidi_resolveMirrorChar (const CHARSETOPS* charset_ops, Glyph32* glyphs, int len, 
        TYPERUN **ptype_rl_list, int base_level)
{
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;

    /* L4. Mirror all characters that are in odd levels and have mirrors. */
    DBGLOG ("6.Mirroring\n");
    for (pp = type_rl_list->next; pp->next; pp = pp->next) {
        if (pp->level & 1)
        {
            int i;
            for (i = POS (pp); i < POS (pp) + LEN (pp); i++)
            {
                Glyph32 mirrored_ch;
                if (charset_ops->bidi_mirror_glyph && charset_ops->bidi_mirror_glyph (glyphs[i], &mirrored_ch))
                    glyphs[i] = mirrored_ch;
            }
        }
    }

    DBGLOG ("  Mirroring, Done\n");
}

static void bidi_resolve_string (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len, int pel,
        TYPERUN **ptype_rl_list, BYTE *pmax_level)
{
    BYTE base_level = 0;
    Uint32 base_dir = BIDI_TYPE_L;
    TYPERUN *type_rl_list = NULL;

    /* split the text to some runs. */
    type_rl_list = get_runtype_link (charset_ops, glyphs, len);

    /* 1.Find base level */
    if (pel != 0 && pel != 1) {
        bidi_resolveParagraphs(&type_rl_list, &base_dir, &base_level);
    }
    else {
        base_level = pel;
        base_dir = BIDI_LEVEL_TO_DIR (base_level);
    }

    /* 2.Resolving Explicit levels.*/
    bidi_resolveExplicit(&type_rl_list, base_dir);

    /* 3.Resolving weak types */
    bidi_resolveWeak(&type_rl_list, base_dir);

    /* 4.Resolving Neutral Types */
    bidi_resolveNeutrals(&type_rl_list, base_dir);

    /* 5.Resolving implicit levels. */
    *pmax_level = bidi_resolveImplicit(&type_rl_list, base_level);

    /* 6.Resolving Mirrored Char. */
    bidi_resolveMirrorChar (charset_ops, glyphs, len, &type_rl_list, base_level);

    *ptype_rl_list = type_rl_list;
}

typedef struct _REORDER_CONTEXT {
    Glyph32* glyphs;
    void* extra;
    CB_REVERSE_EXTRA cb;
} REORDER_CONTEXT;

static void bidi_reverse_glyphs (Glyph32* glyphs, int len, int pos)
{
    int i;
    Glyph32* gs = glyphs + pos;

    for (i = 0; i < len / 2; i++) {
        Glyph32 tmp = gs[i];
        gs[i] = gs[len - 1 - i];
        gs[len - 1 - i] = tmp;
    }
}

static void bidi_reorder (REORDER_CONTEXT* context, int len,
        TYPERUN **ptype_rl_list, BYTE max_level)
{
    int i = 0;
    TYPERUN *type_rl_list = *ptype_rl_list, *pp = NULL;

    /* L2. Reorder. */
    DBGLOG("\n6:Reordering\n");

    for(i = max_level; i > 0; i--){
        for (pp = type_rl_list->next; pp->next; pp = pp->next){
            if (LEVEL (pp) >= i){
                /* Find all stretches that are >= i.*/
                int len = LEN(pp), pos = POS(pp);
                TYPERUN *pp1 = pp->next;
                while (pp1->next && LEVEL(pp1) >= i){
                    len += LEN(pp1);
                    pp1 = pp1->next;
                }
                pp = pp1->prev;

                if (context->glyphs) {
                    bidi_reverse_glyphs(context->glyphs, len, pos);
                }

                if (context->extra && context->cb) {
                    context->cb(context->extra, len, pos);
                }
            }
        }
    }
    DBGLOG("\nReordering, Done\n");
}

static void bidi_reverse_chars (void* context, int len, int pos)
{
    char* str = (char*)context + pos;
    int i;
    for (i = 0; i < len / 2; i++)
    {
        char tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

void __mg_charset_bidi_get_embeddlevels (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len, int pel, Uint8* embedding_levels, Uint8 type)
{
    int i = 0;
    BYTE max_level = 1;
    TYPERUN *type_rl_list = NULL, *pp;

    print_hexstr(glyphs, len, FALSE);

    /* W1~W7, N1~N2, I1~I2, Get the Embedding Level. */
    bidi_resolve_string (charset_ops, glyphs, len, pel, &type_rl_list,
            &max_level);

    /* type = 0, get the logical embedding level; else visual level.*/
    for (pp = type_rl_list->next; pp->next; pp = pp->next){
        int pos = POS (pp), len = LEN (pp);
        for(i = 0; i < len; i++)
            embedding_levels[pos + i] = LEVEL(pp);
    }

    if (type) {
        REORDER_CONTEXT rc;

        rc.glyphs = NULL;
        rc.extra = embedding_levels;
        rc.cb = bidi_reverse_chars;
        bidi_reorder(&rc, len, &type_rl_list, max_level);
    }

    /* free typerun list.*/
    free_typerun_list(type_rl_list);

    print_hexstr(glyphs, len, TRUE);
}

#if 0
static void bidi_map_reverse (void* context, int len, int pos)
{
    GLYPHMAPINFO* str = (GLYPHMAPINFO*)context + pos;
    int i;
    for (i = 0; i < len / 2; i++)
    {
        GLYPHMAPINFO tmp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = tmp;
    }
}

Glyph32* __mg_charset_bidi_map_reorder (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len, GLYPHMAPINFO* map, int pel)
{
    BYTE max_level = 1;
    TYPERUN *type_rl_list = NULL;
    TYPERUN *node_p = NULL;
    int i;
    int run_pos;
    int run_len;

    print_hexstr(glyphs, len, FALSE);

    /* W1~W7, N1~N2, I1~I2, Get the Embedding Level. */
    bidi_resolve_string (charset_ops, glyphs, len, pel, &type_rl_list, &max_level);

    for (node_p=type_rl_list->next; node_p->next; node_p=node_p->next) {
        run_pos = node_p->pos;
        run_len = node_p->len;

        for (i=0; i<run_len; i++)
        {
            (map+run_pos+i)->is_rtol = (node_p)->level & 1;
        }
    }

    bidi_reorder_cb(map, len, &type_rl_list, max_level,
            bidi_map_reverse);

    /* free typerun list.*/
    free_typerun_list(type_rl_list);

    print_hexstr(glyphs, len, TRUE);

    return glyphs;
}

Glyph32* __mg_charset_bidi_index_reorder (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len, int* index_map, int pel)
{
    BYTE max_level = 1;
    TYPERUN *type_rl_list = NULL;

    print_hexstr(glyphs, len, FALSE);

    /* W1~W7, N1~N2, I1~I2, Get the Embedding Level. */
    bidi_resolve_string (charset_ops, glyphs, len, pel, &type_rl_list, &max_level);

    bidi_reorder_cb(index_map, len, &type_rl_list, max_level,
            bidi_string_reverse);

    /* free typerun list.*/
    free_typerun_list(type_rl_list);

    print_hexstr(glyphs, len, TRUE);

    return glyphs;
}
#endif

Glyph32* __mg_charset_bidi_glyphs_reorder (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len, int pel,
        void* extra, CB_REVERSE_EXTRA cb_reverse_extra)
{
    BYTE max_level = 1;
    TYPERUN *type_rl_list = NULL;
    REORDER_CONTEXT rc;

    print_hexstr(glyphs, len, FALSE);

    /* W1~W7, N1~N2, I1~I2, Get the Embedding Level. */
    bidi_resolve_string (charset_ops, glyphs, len, pel, &type_rl_list,
        &max_level);

    rc.glyphs = glyphs;
    rc.extra = extra;
    rc.cb = cb_reverse_extra;
    bidi_reorder (&rc, len, &type_rl_list, max_level);

    /* free typerun list.*/
    free_typerun_list (type_rl_list);

    print_hexstr (glyphs, len, TRUE);

    return glyphs;
}

Uint32 __mg_charset_bidi_str_base_dir (const CHARSETOPS* charset_ops,
        Glyph32* glyphs, int len)
{
    BYTE base_level = 0;
    Uint32 base_dir = BIDI_TYPE_L;
    TYPERUN *type_rl_list = NULL;

    /* split the text to some runs. */
    type_rl_list = get_runtype_link (charset_ops, glyphs, len);

    /* 1.Find base level */
    bidi_resolveParagraphs(&type_rl_list, &base_dir, &base_level);

    /* free typerun list.*/
    free_typerun_list(type_rl_list);

    return base_dir;
}

BOOL GetGlyphBIDIType (LOGFONT* log_font, Glyph32 glyph_value,
        Uint32 *bidi_type)
{
    DEVFONT* mbc_devfont = log_font->mbc_devfont;

    if (mbc_devfont == NULL ||
            mbc_devfont->charset_ops->bidi_glyph_type == NULL)
        return FALSE;

    *bidi_type = mbc_devfont->charset_ops->bidi_glyph_type (glyph_value);
    return TRUE;
}

