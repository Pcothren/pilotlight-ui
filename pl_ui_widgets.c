#include "pl_ui_internal.h"
#include <float.h> // FLT_MAX

//-----------------------------------------------------------------------------
// [SECTION] stb_text mess
//-----------------------------------------------------------------------------

static plVec2
pl__input_text_calc_text_size_w(const plUiWChar* text_begin, const plUiWChar* text_end, const plUiWChar** remaining, plVec2* out_offset, bool stop_on_new_line)
{
    plFont* font = gptCtx->ptFont;
    const float line_height = gptCtx->tStyle.fFontSize;
    const float scale = line_height / font->tConfig.fFontSize;

    plVec2 text_size = {0};
    float line_width = 0.0f;

    const plUiWChar* s = text_begin;
    while (s < text_end)
    {
        unsigned int c = (unsigned int)(*s++);
        if (c == '\n')
        {
            text_size.x = plu_max(text_size.x, line_width);
            text_size.y += line_height;
            line_width = 0.0f;
            if (stop_on_new_line)
                break;
            continue;
        }
        if (c == '\r')
            continue;

        const float char_width = font->sbtGlyphs[font->sbuCodePoints[(plUiWChar)c]].xAdvance * scale;
        line_width += char_width;
    }

    if (text_size.x < line_width)
        text_size.x = line_width;

    if (out_offset)
        *out_offset = (plVec2){line_width, text_size.y + line_height};  // offset allow for the possibility of sitting after a trailing \n

    if (line_width > 0 || text_size.y == 0.0f)                        // whereas size.y will ignore the trailing \n
        text_size.y += line_height;

    if (remaining)
        *remaining = s;

    return text_size;
}


static int     STB_TEXTEDIT_STRINGLEN(const plUiInputTextState* obj)                             { return obj->iCurrentLengthW; }
static plUiWChar STB_TEXTEDIT_GETCHAR(const plUiInputTextState* obj, int idx)                      { return obj->sbTextW[idx]; }
static float   STB_TEXTEDIT_GETWIDTH(plUiInputTextState* obj, int line_start_idx, int char_idx)  { plUiWChar c = obj->sbTextW[line_start_idx + char_idx]; if (c == '\n') return STB_TEXTEDIT_GETWIDTH_NEWLINE; return gptCtx->ptFont->sbtGlyphs[gptCtx->ptFont->sbuCodePoints[c]].xAdvance * (gptCtx->tStyle.fFontSize / gptCtx->ptFont->tConfig.fFontSize); }
static int     STB_TEXTEDIT_KEYTOTEXT(int key)                                                    { return key >= 0x200000 ? 0 : key; }
static plUiWChar STB_TEXTEDIT_NEWLINE = '\n';
static void    STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* r, plUiInputTextState* obj, int line_start_idx)
{
    const plUiWChar* text = obj->sbTextW;
    const plUiWChar* text_remaining = NULL;
    const plVec2 size = pl__input_text_calc_text_size_w(text + line_start_idx, text + obj->iCurrentLengthW, &text_remaining, NULL, true);
    r->x0 = 0.0f;
    r->x1 = size.x;
    r->baseline_y_delta = size.y;
    r->ymin = 0.0f;
    r->ymax = size.y;
    r->num_chars = (int)(text_remaining - (text + line_start_idx));
}

static bool pl__is_separator(unsigned int c)
{
    return c==',' || c==';' || c=='(' || c==')' || c=='{' || c=='}' || c=='[' || c==']' || c=='|' || c=='\n' || c=='\r' || c=='.' || c=='!';
}
static inline bool pl__char_is_blank_w(unsigned int c)  { return c == ' ' || c == '\t' || c == 0x3000; }

static int pl__is_word_boundary_from_right(plUiInputTextState* obj, int idx)
{
    // When PL_UI_INPUT_TEXT_FLAGS_PASSWORD is set, we don't want actions such as CTRL+Arrow to leak the fact that underlying data are blanks or separators.
    // if ((obj->tFlags & PL_UI_INPUT_TEXT_FLAGS_PASSWORD) || idx <= 0)
    //     return 0;

    bool prev_white = pl__char_is_blank_w(obj->sbTextW[idx - 1]);
    bool prev_separ = pl__is_separator(obj->sbTextW[idx - 1]);
    bool curr_white = pl__char_is_blank_w(obj->sbTextW[idx]);
    bool curr_separ = pl__is_separator(obj->sbTextW[idx]);
    return ((prev_white || prev_separ) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
}
static int pl__is_word_boundary_from_left(plUiInputTextState* obj, int idx)
{
    // if ((obj->Flags & ImGuiInputTextFlags_Password) || idx <= 0)
    //     return 0;

    bool prev_white = pl__char_is_blank_w(obj->sbTextW[idx]);
    bool prev_separ = pl__is_separator(obj->sbTextW[idx]);
    bool curr_white = pl__char_is_blank_w(obj->sbTextW[idx - 1]);
    bool curr_separ = pl__is_separator(obj->sbTextW[idx - 1]);
    return ((prev_white) && !(curr_separ || curr_white)) || (curr_separ && !prev_separ);
}
static int  STB_TEXTEDIT_MOVEWORDLEFT_IMPL(plUiInputTextState* obj, int idx)   { idx--; while (idx >= 0 && !pl__is_word_boundary_from_right(obj, idx)) idx--; return idx < 0 ? 0 : idx; }
static int  STB_TEXTEDIT_MOVEWORDRIGHT_MAC(plUiInputTextState* obj, int idx)   { idx++; int len = obj->iCurrentLengthW; while (idx < len && !pl__is_word_boundary_from_left(obj, idx)) idx++; return idx > len ? len : idx; }
static int  STB_TEXTEDIT_MOVEWORDRIGHT_WIN(plUiInputTextState* obj, int idx)   { idx++; int len = obj->iCurrentLengthW; while (idx < len && !pl__is_word_boundary_from_right(obj, idx)) idx++; return idx > len ? len : idx; }
// static int  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL(plUiInputTextState* obj, int idx)  { if (g.IO.ConfigMacOSXBehaviors) return STB_TEXTEDIT_MOVEWORDRIGHT_MAC(obj, idx); else return STB_TEXTEDIT_MOVEWORDRIGHT_WIN(obj, idx); }
static int  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL(plUiInputTextState* obj, int idx)  { return STB_TEXTEDIT_MOVEWORDRIGHT_WIN(obj, idx); }
#define STB_TEXTEDIT_MOVEWORDLEFT   STB_TEXTEDIT_MOVEWORDLEFT_IMPL  // They need to be #define for stb_textedit.h
#define STB_TEXTEDIT_MOVEWORDRIGHT  STB_TEXTEDIT_MOVEWORDRIGHT_IMPL

static inline int pl__text_count_utf8_bytes_from_char(unsigned int c)
{
    if (c < 0x80) return 1;
    if (c < 0x800) return 2;
    if (c < 0x10000) return 3;
    if (c <= 0x10FFFF) return 4;
    return 3;
}

static int pl__text_count_utf8_bytes_from_str(const plUiWChar* in_text, const plUiWChar* in_text_end)
{
    int bytes_count = 0;
    while ((!in_text_end || in_text < in_text_end) && *in_text)
    {
        unsigned int c = (unsigned int)(*in_text++);
        if (c < 0x80)
            bytes_count++;
        else
            bytes_count += pl__text_count_utf8_bytes_from_char(c);
    }
    return bytes_count;
}

static void STB_TEXTEDIT_DELETECHARS(plUiInputTextState* obj, int pos, int n)
{
    plUiWChar* dst = obj->sbTextW + pos;

    // We maintain our buffer length in both UTF-8 and wchar formats
    obj->bEdited = true;
    obj->iCurrentLengthA -= pl__text_count_utf8_bytes_from_str(dst, dst + n);
    obj->iCurrentLengthW -= n;

    // Offset remaining text (FIXME-OPT: Use memmove)
    const plUiWChar* src = obj->sbTextW + pos + n;
    plUiWChar c = *src++;
    while (c)
    {
        *dst++ = c;
        c = *src++;
    }
    *dst = '\0';
}

static bool STB_TEXTEDIT_INSERTCHARS(plUiInputTextState* obj, int pos, const plUiWChar* new_text, int new_text_len)
{
    // const bool is_resizable = (obj->Flags & ImGuiInputTextFlags_CallbackResize) != 0;
    const bool is_resizable = false;
    const int text_len = obj->iCurrentLengthW;
    PL_UI_ASSERT(pos <= text_len);

    const int new_text_len_utf8 = pl__text_count_utf8_bytes_from_str(new_text, new_text + new_text_len);
    if (!is_resizable && (new_text_len_utf8 + obj->iCurrentLengthA + 1 > obj->iBufferCapacityA))
        return false;

    // Grow internal buffer if needed
    if (new_text_len + text_len + 1 > (int)plu_sb_size(obj->sbTextW))
    {
        if (!is_resizable)
            return false;
        PL_UI_ASSERT(text_len < (int)plu_sb_size(obj->sbTextW));
        plu_sb_resize(obj->sbTextW, text_len + plu_clampi(32, new_text_len * 4, plu_max(256, new_text_len)) + 1);
    }

    plUiWChar* text = obj->sbTextW;
    if (pos != text_len)
        memmove(text + pos + new_text_len, text + pos, (size_t)(text_len - pos) * sizeof(plUiWChar));
    memcpy(text + pos, new_text, (size_t)new_text_len * sizeof(plUiWChar));

    obj->bEdited = true;
    obj->iCurrentLengthW += new_text_len;
    obj->iCurrentLengthA += new_text_len_utf8;
    obj->sbTextW[obj->iCurrentLengthW] = '\0';

    return true;
}

// We don't use an enum so we can build even with conflicting symbols (if another user of stb_textedit.h leak their STB_TEXTEDIT_K_* symbols)
#define STB_TEXTEDIT_K_LEFT         0x200000 // keyboard input to move cursor left
#define STB_TEXTEDIT_K_RIGHT        0x200001 // keyboard input to move cursor right
#define STB_TEXTEDIT_K_UP           0x200002 // keyboard input to move cursor up
#define STB_TEXTEDIT_K_DOWN         0x200003 // keyboard input to move cursor down
#define STB_TEXTEDIT_K_LINESTART    0x200004 // keyboard input to move cursor to start of line
#define STB_TEXTEDIT_K_LINEEND      0x200005 // keyboard input to move cursor to end of line
#define STB_TEXTEDIT_K_TEXTSTART    0x200006 // keyboard input to move cursor to start of text
#define STB_TEXTEDIT_K_TEXTEND      0x200007 // keyboard input to move cursor to end of text
#define STB_TEXTEDIT_K_DELETE       0x200008 // keyboard input to delete selection or character under cursor
#define STB_TEXTEDIT_K_BACKSPACE    0x200009 // keyboard input to delete selection or character left of cursor
#define STB_TEXTEDIT_K_UNDO         0x20000A // keyboard input to perform undo
#define STB_TEXTEDIT_K_REDO         0x20000B // keyboard input to perform redo
#define STB_TEXTEDIT_K_WORDLEFT     0x20000C // keyboard input to move cursor left one word
#define STB_TEXTEDIT_K_WORDRIGHT    0x20000D // keyboard input to move cursor right one word
#define STB_TEXTEDIT_K_PGUP         0x20000E // keyboard input to move cursor up a page
#define STB_TEXTEDIT_K_PGDOWN       0x20000F // keyboard input to move cursor down a page
#define STB_TEXTEDIT_K_SHIFT        0x400000

#define STB_TEXTEDIT_IMPLEMENTATION
#include "stb_textedit.h"

// stb_textedit internally allows for a single undo record to do addition and deletion, but somehow, calling
// the stb_textedit_paste() function creates two separate records, so we perform it manually. (FIXME: Report to nothings/stb?)
static void stb_textedit_replace(plUiInputTextState* str, STB_TexteditState* state, const STB_TEXTEDIT_CHARTYPE* text, int text_len)
{
    stb_text_makeundo_replace(str, state, 0, str->iCurrentLengthW, text_len);
    STB_TEXTEDIT_DELETECHARS(str, 0, str->iCurrentLengthW);
    state->cursor = state->select_start = state->select_end = 0;
    if (text_len <= 0)
        return;
    if (STB_TEXTEDIT_INSERTCHARS(str, 0, text, text_len))
    {
        state->cursor = state->select_start = state->select_end = text_len;
        state->has_preferred_x = 0;
        return;
    }
    PL_UI_ASSERT(0); // Failed to insert character, normally shouldn't happen because of how we currently use stb_textedit_replace()
}

static void
pl__text_state_on_key_press(plUiInputTextState* ptState, int iKey)
{
    stb_textedit_key(ptState, &ptState->tStb, iKey);
    ptState->bCursorFollow = true;
    pl__text_state_cursor_anim_reset(ptState);
}

// Based on stb_to_utf8() from github.com/nothings/stb/
static inline int
pl__text_char_to_utf8_inline(char* buf, int buf_size, unsigned int c)
{
    if (c < 0x80)
    {
        buf[0] = (char)c;
        return 1;
    }
    if (c < 0x800)
    {
        if (buf_size < 2) return 0;
        buf[0] = (char)(0xc0 + (c >> 6));
        buf[1] = (char)(0x80 + (c & 0x3f));
        return 2;
    }
    if (c < 0x10000)
    {
        if (buf_size < 3) return 0;
        buf[0] = (char)(0xe0 + (c >> 12));
        buf[1] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[2] = (char)(0x80 + ((c ) & 0x3f));
        return 3;
    }
    if (c <= 0x10FFFF)
    {
        if (buf_size < 4) return 0;
        buf[0] = (char)(0xf0 + (c >> 18));
        buf[1] = (char)(0x80 + ((c >> 12) & 0x3f));
        buf[2] = (char)(0x80 + ((c >> 6) & 0x3f));
        buf[3] = (char)(0x80 + ((c ) & 0x3f));
        return 4;
    }
    // Invalid code point, the max unicode is 0x10FFFF
    return 0;
}

static inline plUiInputTextState*
pl__get_input_text_state(uint32_t id)
{ 
    return (id != 0 && gptCtx->tInputTextState.uId == id) ? &gptCtx->tInputTextState : NULL; 
}

static const plUiWChar*
pl__str_bol_w(const plUiWChar* buf_mid_line, const plUiWChar* buf_begin) // find beginning-of-line
{
    while (buf_mid_line > buf_begin && buf_mid_line[-1] != '\n')
        buf_mid_line--;
    return buf_mid_line;
}

static int
pl__text_str_to_utf8(char* out_buf, int out_buf_size, const plUiWChar* in_text, const plUiWChar* in_text_end)
{
    char* buf_p = out_buf;
    const char* buf_end = out_buf + out_buf_size;
    while (buf_p < buf_end - 1 && (!in_text_end || in_text < in_text_end) && *in_text)
    {
        unsigned int c = (unsigned int)(*in_text++);
        if (c < 0x80)
            *buf_p++ = (char)c;
        else
            buf_p += pl__text_char_to_utf8_inline(buf_p, (int)(buf_end - buf_p - 1), c);
    }
    *buf_p = 0;
    return (int)(buf_p - out_buf);
}

static int
pl__text_str_from_utf8(plUiWChar* buf, int buf_size, const char* in_text, const char* in_text_end, const char** in_text_remaining)
{
    plUiWChar* buf_out = buf;
    plUiWChar* buf_end = buf + buf_size;
    while (buf_out < buf_end - 1 && (!in_text_end || in_text < in_text_end) && *in_text)
    {
        unsigned int c;
        in_text += plu_text_char_from_utf8(&c, in_text, in_text_end);
        *buf_out++ = (plUiWChar)c;
    }
    *buf_out = 0;
    if (in_text_remaining)
        *in_text_remaining = in_text;
    return (int)(buf_out - buf);
}

static int
pl__text_count_chars_from_utf8(const char* in_text, const char* in_text_end)
{
    int char_count = 0;
    while ((!in_text_end || in_text < in_text_end) && *in_text)
    {
        unsigned int c;
        in_text += plu_text_char_from_utf8(&c, in_text, in_text_end);
        char_count++;
    }
    return char_count;
}

static int
pl__input_text_calc_text_len_and_line_count(const char* text_begin, const char** out_text_end)
{
    int line_count = 0;
    const char* s = text_begin;
    char c = *s++;
    while (c) // We are only matching for \n so we can ignore UTF-8 decoding
    {
        if (c == '\n')
            line_count++;
        c = *s++;
    }
    s--;
    if (s[0] != '\n' && s[0] != '\r')
        line_count++;
    *out_text_end = s;
    return line_count;
}

bool
pl_button_behavior(const plRect* ptBox, uint32_t uHash, bool* pbOutHovered, bool* pbOutHeld)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    gptCtx->tPrevItemData.bActive = false;

    bool bPressed = false;
    bool bHovered = pl_is_item_hoverable(ptBox, uHash);

    if(bHovered)
        gptCtx->uNextHoveredId = uHash;

    bool bHeld = bHovered && pl_is_mouse_down(PL_MOUSE_BUTTON_LEFT);

    if(uHash == gptCtx->uActiveId)
    {  
        gptCtx->tPrevItemData.bActive = true;

        if(bHeld)
            gptCtx->uNextActiveId = uHash;
    }

    if(bHovered)
    {
        if(pl_is_mouse_clicked(PL_MOUSE_BUTTON_LEFT, false))
        {
            gptCtx->uNextActiveId = uHash;
            gptCtx->tPrevItemData.bActive = true;
        }
        else if(pl_is_mouse_released(PL_MOUSE_BUTTON_LEFT))
        {
            gptCtx->uNextActiveId = 0;
            bPressed = uHash == gptCtx->uActiveId;
        }
    }

    *pbOutHeld = bHeld;
    *pbOutHovered = bHovered;
    gptCtx->tPrevItemData.bHovered = bHovered;
    return bPressed;
}

bool
pl_button(const char* pcText)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    bool bPressed = false;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
        plRect tBoundingBox = plu_calculate_rect(tStartPos, tWidgetSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);

        bool bHovered = false;
        bool bHeld = false;
        bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonCol);

        const plVec2 tTextSize = pl_ui_calculate_text_size(gptCtx->ptFont, gptCtx->tStyle.fFontSize, pcText, -1.0f);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        plVec2 tTextStartPos = {
            .x = tStartPos.x,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        if((plu_rect_width(&tBoundingBox) < tTextSize.x)) // clipping, so start at beginning of widget
            tTextStartPos.x += gptCtx->tStyle.tFramePadding.x;
        else // not clipping, so center on widget
            tTextStartPos.x += tStartPos.x + tWidgetSize.x / 2.0f - tTextActualCenter.x;

        pl_add_clipped_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, tBoundingBox.tMin, tBoundingBox.tMax, gptCtx->tColorScheme.tTextCol, pcText, 0.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return bPressed;
}

bool
pl_selectable(const char* pcText, bool* bpValue)
{

    // temporary hack
    static bool bDummyState = true;
    if(bpValue == NULL) bpValue = &bDummyState;

    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    bool bPressed = false;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));

        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        const plVec2 tTextStartPos = {
            .x = tStartPos.x + gptCtx->tStyle.tFramePadding.x,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        const plVec2 tEndPos = plu_add_vec2(tStartPos, tWidgetSize);

        plRect tBoundingBox = plu_calculate_rect(tStartPos, tWidgetSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(bPressed)
            *bpValue = !*bpValue;

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tEndPos, gptCtx->tColorScheme.tHeaderActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tEndPos, gptCtx->tColorScheme.tHeaderHoveredCol);

        if(*bpValue)
            pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tEndPos, gptCtx->tColorScheme.tHeaderCol);

        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return bPressed; 
}

bool
pl_checkbox(const char* pcText, bool* bpValue)
{
    // temporary hack
    static bool bDummyState = true;
    if(bpValue == NULL) bpValue = &bDummyState;

    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    bool bPressed = false;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const bool bOriginalValue = *bpValue;
        const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);


        const plVec2 tTextStartPos = {
            .x = tStartPos.x + tWidgetSize.y + gptCtx->tStyle.tInnerSpacing.x,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        plRect tBoundingBox = plu_calculate_rect(tStartPos, (plVec2){tWidgetSize.y, tWidgetSize.y});
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(bPressed)
            *bpValue = !bOriginalValue;

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgCol);

        if(*bpValue)
            pl_add_line(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tCheckmarkCol, 2.0f);

        // add label
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f); 
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return bPressed;
}

bool
pl_radio_button(const char* pcText, int* piValue, int iButtonValue)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    bool bPressed = false;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
        const plVec2 tTextSize = pl_ui_calculate_text_size(gptCtx->ptFont, gptCtx->tStyle.fFontSize, pcText, -1.0f);
        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        const plVec2 tSize = {tTextSize.x + 2.0f * gptCtx->tStyle.tFramePadding.x + gptCtx->tStyle.tInnerSpacing.x + tWidgetSize.y, tWidgetSize.y};
        // tSize = plu_floor_vec2(tSize);

        const plVec2 tTextStartPos = {
            .x = tStartPos.x + tWidgetSize.y + gptCtx->tStyle.tInnerSpacing.x + gptCtx->tStyle.tFramePadding.x,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        plRect tBoundingBox = plu_rect_expand_vec2(&tTextBounding, (plVec2){0.5f * (gptCtx->tStyle.tFramePadding.x + gptCtx->tStyle.tInnerSpacing.x + tWidgetSize.y), 0.0f});
        tBoundingBox = plu_rect_move_start_x(&tBoundingBox, tStartPos.x + gptCtx->tStyle.tFramePadding.x);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(bPressed)
            *piValue = iButtonValue;

        if(gptCtx->uActiveId == uHash)       pl_add_circle_filled(ptWindow->ptFgLayer, (plVec2){tStartPos.x + tWidgetSize.y / 2.0f, tStartPos.y + tWidgetSize.y / 2.0f}, gptCtx->tStyle.fFontSize / 1.5f, gptCtx->tColorScheme.tFrameBgActiveCol, 12);
        else if(gptCtx->uHoveredId == uHash) pl_add_circle_filled(ptWindow->ptFgLayer, (plVec2){tStartPos.x + tWidgetSize.y / 2.0f, tStartPos.y + tWidgetSize.y / 2.0f}, gptCtx->tStyle.fFontSize / 1.5f, gptCtx->tColorScheme.tFrameBgHoveredCol, 12);
        else                                 pl_add_circle_filled(ptWindow->ptFgLayer, (plVec2){tStartPos.x + tWidgetSize.y / 2.0f, tStartPos.y + tWidgetSize.y / 2.0f}, gptCtx->tStyle.fFontSize / 1.5f, gptCtx->tColorScheme.tFrameBgCol, 12);

        if(*piValue == iButtonValue)
            pl_add_circle_filled(ptWindow->ptFgLayer, (plVec2){tStartPos.x + tWidgetSize.y / 2.0f, tStartPos.y + tWidgetSize.y / 2.0f}, gptCtx->tStyle.fFontSize / 2.5f, gptCtx->tColorScheme.tCheckmarkCol, 12);

        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return bPressed;
}

bool
pl_collapsing_header(const char* pcText)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
    bool* pbOpenState = pl_get_bool_ptr(&ptWindow->tStorage, uHash, false);
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        const plVec2 tTextStartPos = {
            .x = tStartPos.x + tWidgetSize.y * 1.5f,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        plRect tBoundingBox = plu_calculate_rect(tStartPos, tWidgetSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        const bool bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(bPressed)
            *pbOpenState = !*pbOpenState;

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tHeaderActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tHeaderHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tHeaderCol);

        if(*pbOpenState)
        {
            const plVec2 centerPoint = {tStartPos.x + 8.0f * 1.5f, tStartPos.y + tWidgetSize.y / 2.0f};
            const plVec2 pointPos = plu_add_vec2(centerPoint, (plVec2){ 0.0f,  4.0f});
            const plVec2 rightPos = plu_add_vec2(centerPoint, (plVec2){ 4.0f, -4.0f});
            const plVec2 leftPos  = plu_add_vec2(centerPoint,  (plVec2){-4.0f, -4.0f});
            pl_add_triangle_filled(ptWindow->ptFgLayer, pointPos, rightPos, leftPos, (plVec4){1.0f, 1.0f, 1.0f, 1.0f});
        }
        else
        {
            const plVec2 centerPoint = {tStartPos.x + 8.0f * 1.5f, tStartPos.y + tWidgetSize.y / 2.0f};
            const plVec2 pointPos = plu_add_vec2(centerPoint, (plVec2){  4.0f,  0.0f});
            const plVec2 rightPos = plu_add_vec2(centerPoint, (plVec2){ -4.0f, -4.0f});
            const plVec2 leftPos  = plu_add_vec2(centerPoint, (plVec2){ -4.0f,  4.0f});
            pl_add_triangle_filled(ptWindow->ptFgLayer, pointPos, rightPos, leftPos, (plVec4){1.0f, 1.0f, 1.0f, 1.0f});
        }

        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f);
    }
    if(*pbOpenState)
        plu_sb_push(ptWindow->sbtRowStack, ptWindow->tTempData.tCurrentLayoutRow);
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return *pbOpenState; 
}

void
pl_end_collapsing_header(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    ptWindow->tTempData.tCurrentLayoutRow = plu_sb_pop(ptWindow->sbtRowStack);
}

bool
pl_tree_node(const char* pcText)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plu_sb_push(ptWindow->sbtRowStack, ptWindow->tTempData.tCurrentLayoutRow);
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
    
    bool* pbOpenState = pl_get_bool_ptr(&ptWindow->tStorage, uHash, false);
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {

        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        plRect tBoundingBox = plu_calculate_rect(tStartPos, tWidgetSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        const bool bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(bPressed)
            *pbOpenState = !*pbOpenState;

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tHeaderActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tHeaderHoveredCol);

        if(*pbOpenState)
        {
            const plVec2 centerPoint = {tStartPos.x + 8.0f * 1.5f, tStartPos.y + tWidgetSize.y / 2.0f};
            const plVec2 pointPos = plu_add_vec2(centerPoint, (plVec2){ 0.0f,  4.0f});
            const plVec2 rightPos = plu_add_vec2(centerPoint, (plVec2){ 4.0f, -4.0f});
            const plVec2 leftPos  = plu_add_vec2(centerPoint,  (plVec2){-4.0f, -4.0f});
            pl_add_triangle_filled(ptWindow->ptFgLayer, pointPos, rightPos, leftPos, (plVec4){1.0f, 1.0f, 1.0f, 1.0f}); 
        }
        else
        {
            const plVec2 centerPoint = {tStartPos.x + 8.0f * 1.5f, tStartPos.y + tWidgetSize.y / 2.0f};
            const plVec2 pointPos = plu_add_vec2(centerPoint, (plVec2){  4.0f,  0.0f});
            const plVec2 rightPos = plu_add_vec2(centerPoint, (plVec2){ -4.0f, -4.0f});
            const plVec2 leftPos  = plu_add_vec2(centerPoint, (plVec2){ -4.0f,  4.0f});
            pl_add_triangle_filled(ptWindow->ptFgLayer, pointPos, rightPos, leftPos, (plVec4){1.0f, 1.0f, 1.0f, 1.0f});
        }

        const plVec2 tTextStartPos = {
            .x = tStartPos.x + tWidgetSize.y * 1.5f,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);

    if(*pbOpenState)
    {
        ptWindow->tTempData.uTreeDepth++;
        plu_sb_push(ptWindow->sbtRowStack, ptWindow->tTempData.tCurrentLayoutRow);
        plu_sb_push(gptCtx->sbuIdStack, uHash);
    }

    return *pbOpenState; 
}

bool
pl_tree_node_f(const char* pcFmt, ...)
{
    va_list args;
    va_start(args, pcFmt);
    bool bOpen = pl_tree_node_v(pcFmt, args);
    va_end(args);
    return bOpen;
}

bool
pl_tree_node_v(const char* pcFmt, va_list args)
{
    static char acTempBuffer[1024];
    plu_vsprintf(acTempBuffer, pcFmt, args);
    return pl_tree_node(acTempBuffer);
}

void
pl_tree_pop(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    ptWindow->tTempData.uTreeDepth--;
    plu_sb_pop(gptCtx->sbuIdStack);
    ptWindow->tTempData.tCurrentLayoutRow = plu_sb_pop(ptWindow->sbtRowStack);
}

bool
pl_begin_tab_bar(const char* pcText)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const float fFrameHeight = pl_get_frame_height();

    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());

    plu_sb_push(ptWindow->sbtRowStack, ptWindow->tTempData.tCurrentLayoutRow);
    ptWindow->tTempData.tCurrentLayoutRow.fRowStartX = tStartPos.x - ptWindow->tTempData.tRowPos.x;
    ptWindow->tTempData.fAccumRowX += ptWindow->tTempData.tCurrentLayoutRow.fRowStartX;
    
    pl_layout_dynamic(0.0f, 1);
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    
    const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
    plu_sb_push(gptCtx->sbuIdStack, uHash);

    // check if tab bar existed
    gptCtx->ptCurrentTabBar = NULL;
    for(uint32_t i = 0; i < plu_sb_size(gptCtx->sbtTabBars); i++)
    {
        if(gptCtx->sbtTabBars[i].uId == uHash)
        {
            gptCtx->ptCurrentTabBar = &gptCtx->sbtTabBars[i];
            break;
        }
    }

    // new tab bar needs to be created
    if(gptCtx->ptCurrentTabBar == NULL)
    {
        plUiTabBar tTabBar = {
            .uId = uHash
        };;

        plu_sb_push(gptCtx->sbtTabBars, tTabBar);
        gptCtx->ptCurrentTabBar = &plu_sb_top(gptCtx->sbtTabBars);
    }

    gptCtx->ptCurrentTabBar->tStartPos = tStartPos;
    gptCtx->ptCurrentTabBar->tCursorPos = tStartPos;
    gptCtx->ptCurrentTabBar->uCurrentIndex = 0u;

    pl_add_line(ptWindow->ptFgLayer, 
        (plVec2){gptCtx->ptCurrentTabBar->tStartPos.x, gptCtx->ptCurrentTabBar->tStartPos.y + fFrameHeight},
        (plVec2){gptCtx->ptCurrentTabBar->tStartPos.x + tWidgetSize.x, gptCtx->ptCurrentTabBar->tStartPos.y + fFrameHeight},
        gptCtx->tColorScheme.tButtonActiveCol, 1.0f);

    pl_advance_cursor(tWidgetSize.x, fFrameHeight);
    return true;
}

void
pl_end_tab_bar(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    gptCtx->ptCurrentTabBar->uValue = gptCtx->ptCurrentTabBar->uNextValue;
    plu_sb_pop(gptCtx->sbuIdStack);
    ptWindow->tTempData.tCurrentLayoutRow = plu_sb_pop(ptWindow->sbtRowStack);
    ptWindow->tTempData.fAccumRowX -= ptWindow->tTempData.tCurrentLayoutRow.fRowStartX;
}

bool
pl_begin_tab(const char* pcText)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plu_sb_push(ptWindow->sbtRowStack, ptWindow->tTempData.tCurrentLayoutRow);
    pl_layout_dynamic(0.0f, 1);
    plUiTabBar* ptTabBar = gptCtx->ptCurrentTabBar;
    const float fFrameHeight = pl_get_frame_height();
    const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
    plu_sb_push(gptCtx->sbuIdStack, uHash);

    if(ptTabBar->uValue == 0u) ptTabBar->uValue = uHash;

    const plVec2 tTextSize = pl_ui_calculate_text_size(gptCtx->ptFont, gptCtx->tStyle.fFontSize, pcText, -1.0f);
    const plVec2 tStartPos = ptTabBar->tCursorPos;

    plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcText, pl_find_renderered_text_end(pcText, NULL), -1.0f);
    const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

    const plVec2 tFinalSize = {tTextSize.x + 2.0f * gptCtx->tStyle.tFramePadding.x, fFrameHeight};

    const plVec2 tTextStartPos = {
        .x = tStartPos.x + tStartPos.x + tFinalSize.x / 2.0f - tTextActualCenter.x,
        .y = tStartPos.y + tStartPos.y + fFrameHeight / 2.0f - tTextActualCenter.y
    };

    const plRect tBoundingBox = plu_calculate_rect(tStartPos, tFinalSize);
    bool bHovered = false;
    bool bHeld = false;
    const bool bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

    if(uHash == gptCtx->uActiveId)
    {
        ptTabBar->uNextValue = uHash;
    }

    if(gptCtx->uActiveId== uHash)        pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonActiveCol);
    else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonHoveredCol);
    else if(ptTabBar->uValue == uHash)   pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonActiveCol);
    else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tButtonCol);
    
    pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcText, -1.0f);

    ptTabBar->tCursorPos.x += gptCtx->tStyle.tInnerSpacing.x + tFinalSize.x;
    ptTabBar->uCurrentIndex++;

    if(ptTabBar->uValue != uHash)
        pl_end_tab();

    return ptTabBar->uValue == uHash;
}

void
pl_end_tab(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plu_sb_pop(gptCtx->sbuIdStack);
    ptWindow->tTempData.tCurrentLayoutRow = plu_sb_pop(ptWindow->sbtRowStack);
}

void
pl_separator(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;

    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    const plVec2 tWidgetSize = pl_calculate_item_size(gptCtx->tStyle.tItemSpacing.y * 2.0f);
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
        pl_add_line(ptWindow->ptFgLayer, tStartPos, (plVec2){tStartPos.x + tWidgetSize.x, tStartPos.y}, gptCtx->tColorScheme.tCheckmarkCol, 1.0f);

    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
}

void
pl_vertical_spacing(void)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    ptWindow->tTempData.tRowPos.y += gptCtx->tStyle.tItemSpacing.y * 2.0f;
}

void
pl_indent(float fIndent)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    ptCurrentRow->fHorizontalOffset += fIndent == 0.0f ? gptCtx->tStyle.fIndentSize : fIndent;
    ptWindow->tTempData.fExtraIndent += fIndent == 0.0f ? gptCtx->tStyle.fIndentSize : fIndent;
}

void
pl_unindent(float fIndent)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    ptCurrentRow->fHorizontalOffset -= fIndent == 0.0f ? gptCtx->tStyle.fIndentSize : fIndent;
    ptWindow->tTempData.fExtraIndent -= fIndent == 0.0f ? gptCtx->tStyle.fIndentSize : fIndent;
}


void
pl_text(const char* pcFmt, ...)
{
    va_list args;
    va_start(args, pcFmt);
    pl_text_v(pcFmt, args);
    va_end(args);
}

void
pl_text_v(const char* pcFmt, va_list args)
{
    static char acTempBuffer[1024];
    
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        plu_vsprintf(acTempBuffer, pcFmt, args);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, acTempBuffer, pl_find_renderered_text_end(acTempBuffer, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y}, gptCtx->tColorScheme.tTextCol, acTempBuffer, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
}

void
pl_color_text(plVec4 tColor, const char* pcFmt, ...)
{
    va_list args;
    va_start(args, pcFmt);
    pl_color_text_v(tColor, pcFmt, args);
    va_end(args);
}

void
pl_color_text_v(plVec4 tColor, const char* pcFmt, va_list args)
{
    static char acTempBuffer[1024];
    
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        plu_vsprintf(acTempBuffer, pcFmt, args);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, acTempBuffer, pl_find_renderered_text_end(acTempBuffer, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y}, tColor, acTempBuffer, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
}

void
pl_labeled_text(const char* pcLabel, const char* pcFmt, ...)
{
    va_list args;
    va_start(args, pcFmt);
    pl_labeled_text_v(pcLabel, pcFmt, args);
    va_end(args);
}

void
pl_labeled_text_v(const char* pcLabel, const char* pcFmt, va_list args)
{
    static char acTempBuffer[1024];
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        plu_vsprintf(acTempBuffer, pcFmt, args);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, acTempBuffer, pl_find_renderered_text_end(acTempBuffer, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        const plVec2 tStartLocation = {tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y};
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, plu_add_vec2(tStartLocation, (plVec2){(tWidgetSize.x / 3.0f), 0.0f}), gptCtx->tColorScheme.tTextCol, acTempBuffer, -1.0f);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartLocation, gptCtx->tColorScheme.tTextCol, pcLabel, -1.0f);
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
}

bool
pl_input_text_ex(const char* pcLabel, const char* pcHint, char* pcBuffer, size_t szBufferSize, plUiInputTextFlags tFlags);

bool
pl_input_text(const char* pcLabel, char* pcBuffer, size_t szBufferSize)
{
    return pl_input_text_ex(pcLabel, NULL, pcBuffer, szBufferSize, 0);
}

bool
pl_input_text_hint(const char* pcLabel, const char* pcHint, char* pcBuffer, size_t szBufferSize)
{
    return pl_input_text_ex(pcLabel, pcHint, pcBuffer, szBufferSize, 0);
}

bool
pl_input_float(const char* pcLabel, float* pfValue, const char* pcFormat)
{
    char acBuffer[64] = {0};
    plu_sprintf(acBuffer, pcFormat, *pfValue);
    const bool bChanged = pl_input_text_ex(pcLabel, NULL, acBuffer, 64, PL_UI_INPUT_TEXT_FLAGS_CHARS_SCIENTIFIC);

    if(bChanged)
        *pfValue = (float)atof(acBuffer);

    return bChanged;
}

bool
pl_input_int(const char* pcLabel, int* piValue)
{
    char acBuffer[64] = {0};
    plu_sprintf(acBuffer, "%d", *piValue);
    const bool bChanged = pl_input_text_ex(pcLabel, NULL, acBuffer, 64, PL_UI_INPUT_TEXT_FLAGS_CHARS_DECIMAL);

    if(bChanged)
        *piValue = atoi(acBuffer);

    return bChanged;
}

bool
pl_input_text_ex(const char* pcLabel, const char* pcHint, char* pcBuffer, size_t szBufferSize, plUiInputTextFlags tFlags)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tMousePos = pl_get_mouse_pos();

    const bool RENDER_SELECTION_WHEN_INACTIVE = false;
    const bool bIsMultiLine = (tFlags & PL_UI_INPUT_TEXT_FLAGS_MULTILINE) != 0;
    const bool bIsReadOnly  = (tFlags & PL_UI_INPUT_TEXT_FLAGS_READ_ONLY) != 0;
    const bool bIsPassword  = (tFlags & PL_UI_INPUT_TEXT_FLAGS_PASSWORD) != 0;
    const bool bIsUndoable  = (tFlags & PL_UI_INPUT_TEXT_FLAGS_NO_UNDO_REDO) != 0;
    const bool bIsResizable = (tFlags & PL_UI_INPUT_TEXT_FLAGS_CALLBACK_RESIZE) != 0;

    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    const plVec2 tFrameStartPos = {floorf(tStartPos.x + (tWidgetSize.x / 3.0f)), tStartPos.y };
    const uint32_t uHash = plu_str_hash(pcLabel, 0, plu_sb_top(gptCtx->sbuIdStack));

    const plRect tLabelTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, pcLabel, pl_find_renderered_text_end(pcLabel, NULL), -1.0f);
    const plVec2 tLabelTextActualCenter = plu_rect_center(&tLabelTextBounding);

    const plVec2 tFrameSize = { 2.0f * (tWidgetSize.x / 3.0f), tWidgetSize.y};
    plRect tBoundingBox = plu_calculate_rect(tFrameStartPos, tFrameSize);
    const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
    tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);

    plUiWindow* ptDrawWindow = ptWindow;
    plVec2 tInnerSize = tFrameSize;

    const bool bHovered = pl_is_item_hoverable(&tBoundingBox, uHash);

    if(bHovered)
    {
        pl_set_mouse_cursor(PL_MOUSE_CURSOR_TEXT_INPUT);
        gptCtx->uNextHoveredId = uHash;
    }

    plUiInputTextState* ptState = pl__get_input_text_state(uHash);

    // TODO: scroll stuff
    const bool bUserClicked = bHovered && pl_is_mouse_clicked(0, false);
    const bool bUserScrollFinish = false; // bIsMultiLine && ptState != NULL && gptCtx->uNextActiveId == 0 && gptCtx->uActiveId == GetWindowScrollbarID(ptDrawWindow, ImGuiAxis_Y);
    const bool bUserScrollActive = false; // bIsMultiLine && ptState != NULL && gptCtx->uNextActiveId == GetWindowScrollbarID(ptDrawWindow, ImGuiAxis_Y);

    bool bClearActiveId = false;
    bool bSelectAll = false;

    float fScrollY = bIsMultiLine ? ptDrawWindow->tScroll.y : FLT_MAX;

    const bool bInitChangedSpecs = (ptState != NULL && ptState->tStb.single_line != !bIsMultiLine); // state != NULL means its our state.
    const bool bInitMakeActive = (bUserClicked || bUserScrollFinish);
    const bool bInitState = (bInitMakeActive || bUserScrollActive);
    if((bUserClicked && gptCtx->uActiveId != uHash) || bInitChangedSpecs)
    {
        // Access state even if we don't own it yet.
        ptState = &gptCtx->tInputTextState;
        pl__text_state_cursor_anim_reset(ptState);

        // Take a copy of the initial buffer value (both in original UTF-8 format and converted to wchar)
        // From the moment we focused we are ignoring the content of 'buf' (unless we are in read-only mode)
        const int iBufferLength = (int)strlen(pcBuffer);
        plu_sb_resize(ptState->sbInitialTextA, iBufferLength + 1); // UTF-8. we use +1 to make sure that it is always pointing to at least an empty string.
        memcpy(ptState->sbInitialTextA, pcBuffer, iBufferLength + 1);

        // Preserve cursor position and undo/redo stack if we come back to same widget
        // FIXME: Since we reworked this on 2022/06, may want to differenciate recycle_cursor vs recycle_undostate?
        bool bRecycleState = (ptState->uId == uHash && !bInitChangedSpecs);
        if (bRecycleState && (ptState->iCurrentLengthA != iBufferLength || (ptState->bTextAIsValid && strncmp(ptState->sbTextA, pcBuffer, iBufferLength) != 0)))
            bRecycleState = false;

        // start edition
        const char* pcBufferEnd = NULL;
        ptState->uId = uHash;
        plu_sb_resize(ptState->sbTextW, (uint32_t)szBufferSize + 1);          // wchar count <= UTF-8 count. we use +1 to make sure that .Data is always pointing to at least an empty string.
        plu_sb_reset(ptState->sbTextA);
        ptState->bTextAIsValid = false;                // TextA is not valid yet (we will display buf until then)
        ptState->iCurrentLengthW = pl__text_str_from_utf8(ptState->sbTextW, (int)szBufferSize, pcBuffer, NULL, &pcBufferEnd);
        ptState->iCurrentLengthA = (int)(pcBufferEnd - pcBuffer);      // We can't get the result from ImStrncpy() above because it is not UTF-8 aware. Here we'll cut off malformed UTF-8.

        if (bRecycleState)
        {
            // Recycle existing cursor/selection/undo stack but clamp position
            // Note a single mouse click will override the cursor/position immediately by calling stb_textedit_click handler.
            pl__text_state_cursor_clamp(ptState);
        }
        else
        {
            ptState->fScrollX = 0.0f;
            stb_textedit_initialize_state(&ptState->tStb, !bIsMultiLine);
        }

        if (!bIsMultiLine)
        {
            if (tFlags & PL_UI_INPUT_TEXT_FLAGS_AUTO_SELECT_ALL)
                bSelectAll = true;
            // if (input_requested_by_nav && (!bRecycleState || !(g.NavActivateFlags & ImGuiActivateFlags_TryToPreserveState)))
            //     bSelectAll = true;
            // if (input_requested_by_tabbing || (bUserClicked &&gptCtx->tIO.bKeyCtrl))
            //     bSelectAll = true;
        }

        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_ALWAYS_OVERWRITE)
            ptState->tStb.insert_mode = 1; // stb field name is indeed incorrect (see #2863)

    }

    const bool bIsOsX = gptCtx->tIO.bConfigMacOSXBehaviors;
    if (gptCtx->uActiveId != uHash && bInitMakeActive)
    {
        PL_UI_ASSERT(ptState && ptState->uId == uHash);
        gptCtx->tPrevItemData.bActive = true;
        gptCtx->uNextActiveId = uHash;
        // SetActiveID(id, window);
        // SetFocusID(id, window);
        // FocusWindow(window);
    }
    if (gptCtx->uActiveId == uHash)
    {
        gptCtx->tPrevItemData.bActive = true;
        gptCtx->uNextActiveId = uHash;
        // Declare some inputs, the other are registered and polled via Shortcut() routing system.
        // if (bUserClicked)
        //     SetKeyOwner(PL_KEY_MouseLeft, id);
        // g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Left) | (1 << ImGuiDir_Right);
        // if (bIsMultiLine || (flags & ImGuiInputTextFlags_CallbackHistory))
        //     g.ActiveIdUsingNavDirMask |= (1 << ImGuiDir_Up) | (1 << ImGuiDir_Down);
        // SetKeyOwner(PL_KEY_Home, id);
        // SetKeyOwner(PL_KEY_End, id);
        // if (bIsMultiLine)
        // {
        //     SetKeyOwner(PL_KEY_PageUp, id);
        //     SetKeyOwner(PL_KEY_PageDown, id);
        // }
        // if (bIsOsX)
        //     SetKeyOwner(ImGuiMod_Alt, id);
        // if (flags & (ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_AllowTabInput)) // Disable keyboard tabbing out as we will use the \t character.
        //     SetShortcutRouting(PL_KEY_Tab, id);
    }

    // We have an edge case if ActiveId was set through another widget (e.g. widget being swapped), clear id immediately (don't wait until the end of the function)
    if (gptCtx->uActiveId == uHash && ptState == NULL)
        gptCtx->uNextActiveId = 0;

    // Release focus when we click outside
    if (gptCtx->uActiveId == uHash && pl_is_mouse_clicked(0, false) && !bInitState && !bInitMakeActive) //-V560
        bClearActiveId = true;

    // Lock the decision of whether we are going to take the path displaying the cursor or selection
    bool bRenderCursor = (gptCtx->uActiveId == uHash) || (ptState && bUserScrollActive);
    bool bRenderSelection = ptState && (pl__text_state_has_selection(ptState) || bSelectAll) && (RENDER_SELECTION_WHEN_INACTIVE || bRenderCursor);
    bool bValueChanged = false;
    bool bValidated = false;

    // When read-only we always use the live data passed to the function
    // FIXME-OPT: Because our selection/cursor code currently needs the wide text we need to convert it when active, which is not ideal :(
    if (bIsReadOnly && ptState != NULL && (bRenderCursor || bRenderSelection))
    {
        const char* pcBufferEnd = NULL;
        plu_sb_resize(ptState->sbTextW, (uint32_t)szBufferSize + 1);
        ptState->iCurrentLengthW = pl__text_str_from_utf8(ptState->sbTextW, plu_sb_size(ptState->sbTextW), pcBuffer, NULL, &pcBufferEnd);
        ptState->iCurrentLengthA = (int)(pcBufferEnd - pcBuffer);
        pl__text_state_cursor_clamp(ptState);
        bRenderSelection &= pl__text_state_has_selection(ptState);
    }

    // Select the buffer to render.
    const bool bBufferDisplayFromState = (bRenderCursor || bRenderSelection || gptCtx->uActiveId == uHash) && !bIsReadOnly && ptState && ptState->bTextAIsValid;
    const bool bIsDisplayingHint = (pcHint != NULL && (bBufferDisplayFromState ? ptState->sbTextA : pcBuffer)[0] == 0);

    // Password pushes a temporary font with only a fallback glyph
    if (bIsPassword && !bIsDisplayingHint)
    {
        // const ImFontGlyph* glyph = g.Font->FindGlyph('*');
        // ImFont* password_font = &g.InputTextPasswordFont;
        // password_font->FontSize = g.Font->FontSize;
        // password_font->Scale = g.Font->Scale;
        // password_font->Ascent = g.Font->Ascent;
        // password_font->Descent = g.Font->Descent;
        // password_font->ContainerAtlas = g.Font->ContainerAtlas;
        // password_font->FallbackGlyph = glyph;
        // password_font->FallbackAdvanceX = glyph->AdvanceX;
        // PL_UI_ASSERT(password_font->Glyphs.empty() && password_font->IndexAdvanceX.empty() && password_font->IndexLookup.empty());
        // PushFont(password_font);
    }

    // process mouse inputs and character inputs
    int iBackupCurrentTextLength = 0;
    if(gptCtx->uActiveId == uHash)
    {
        PL_UI_ASSERT(ptState != NULL);
        iBackupCurrentTextLength = ptState->iCurrentLengthA;
        ptState->bEdited = false;
        ptState->iBufferCapacityA = (int)szBufferSize;
        ptState->tFlags = tFlags;

        // Although we are active we don't prevent mouse from hovering other elements unless we are interacting right now with the widget.
        // Down the line we should have a cleaner library-wide concept of Selected vs Active.
        // g.ActiveIdAllowOverlap = !io.MouseDown[0];

        // Edit in progress
        const float fMouseX = (tMousePos.x - tBoundingBox.tMin.x - gptCtx->tStyle.tFramePadding.x) + ptState->fScrollX;
        const float fMouseY = (bIsMultiLine ? (tMousePos.y - tBoundingBox.tMin.y) : (gptCtx->tStyle.fFontSize * 0.5f));

        if (bSelectAll)
        {
            pl__text_state_select_all(ptState);
            ptState->bSelectedAllMouseLock = true;
        }
        else if (bHovered && gptCtx->tIO._auMouseClickedCount[0] >= 2 && !gptCtx->tIO.bKeyShift)
        {
            stb_textedit_click(ptState, &ptState->tStb, fMouseX, fMouseY);
            const int iMultiClipCount = (gptCtx->tIO._auMouseClickedCount[0] - 2);
            if ((iMultiClipCount % 2) == 0)
            {
                // Double-click: Select word
                // We always use the "Mac" word advance for double-click select vs CTRL+Right which use the platform dependent variant:
                // FIXME: There are likely many ways to improve this behavior, but there's no "right" behavior (depends on use-case, software, OS)
                const bool bIsBol = (ptState->tStb.cursor == 0) || STB_TEXTEDIT_GETCHAR(ptState, ptState->tStb.cursor - 1) == '\n';
                if (STB_TEXT_HAS_SELECTION(&ptState->tStb) || !bIsBol)
                    pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_WORDLEFT);
                if (!STB_TEXT_HAS_SELECTION(&ptState->tStb))
                    stb_textedit_prep_selection_at_cursor(&ptState->tStb);
                ptState->tStb.cursor = STB_TEXTEDIT_MOVEWORDRIGHT_MAC(ptState, ptState->tStb.cursor);
                ptState->tStb.select_end = ptState->tStb.cursor;
                stb_textedit_clamp(ptState, &ptState->tStb);
            }
            else
            {
                // Triple-click: Select line
                const bool is_eol = STB_TEXTEDIT_GETCHAR(ptState, ptState->tStb.cursor) == '\n';
                pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_LINESTART);
                pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_LINEEND | STB_TEXTEDIT_K_SHIFT);
                pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_RIGHT | STB_TEXTEDIT_K_SHIFT);
                if (!is_eol && bIsMultiLine)
                {
                    int iOne = ptState->tStb.select_start;
                    int iTwo = ptState->tStb.select_end;
                    ptState->tStb.select_start = iTwo;
                    ptState->tStb.select_end = iOne;
                    ptState->tStb.cursor = ptState->tStb.select_end;
                }
                ptState->bCursorFollow = false;
            }
            pl__text_state_cursor_anim_reset(ptState);
        }
        else if (gptCtx->tIO._abMouseClicked[0] && !ptState->bSelectedAllMouseLock)
        {
            if (bHovered)
            {
                if (gptCtx->tIO.bKeyShift)
                    stb_textedit_drag(ptState, &ptState->tStb, fMouseX, fMouseY);
                else
                    stb_textedit_click(ptState, &ptState->tStb, fMouseX, fMouseY);
                pl__text_state_cursor_anim_reset(ptState);
            }
        }
        else if (gptCtx->tIO._abMouseDown[0] && !ptState->bSelectedAllMouseLock && (gptCtx->tIO._tMouseDelta.x != 0.0f || gptCtx->tIO._tMouseDelta.y != 0.0f))
        {
            stb_textedit_drag(ptState, &ptState->tStb, fMouseX, fMouseY);
            pl__text_state_cursor_anim_reset(ptState);
            ptState->bCursorFollow = true;
        }
        if (ptState->bSelectedAllMouseLock && !gptCtx->tIO._abMouseDown[0])
            ptState->bSelectedAllMouseLock = false;

        // We expect backends to emit a Tab key but some also emit a Tab character which we ignore (#2467, #1336)
        // (For Tab and Enter: Win32/SFML/Allegro are sending both keys and chars, GLFW and SDL are only sending keys. For Space they all send all threes)
        if ((tFlags & PL_UI_INPUT_TEXT_FLAGS_ALLOW_TAB_INPUT) && pl_is_key_down(PL_KEY_TAB) && !bIsReadOnly)
        {
            unsigned int c = '\t'; // Insert TAB
            if (pl__input_text_filter_character(&c, tFlags))
                pl__text_state_on_key_press(ptState, (int)c);
        }

        // Process regular text input (before we check for Return because using some IME will effectively send a Return?)
        // We ignore CTRL inputs, but need to allow ALT+CTRL as some keyboards (e.g. German) use AltGR (which _is_ Alt+Ctrl) to input certain characters.
        const bool bIgnoreCharInputs = (gptCtx->tIO.bKeyCtrl && !gptCtx->tIO.bKeyAlt) || (bIsOsX && gptCtx->tIO.bKeySuper);

        if (plu_sb_size(gptCtx->tIO._sbInputQueueCharacters) > 0)
        {
            if (!bIgnoreCharInputs && !bIsReadOnly) // && input_requested_by_nav
                for (uint32_t n = 0; n < plu_sb_size(gptCtx->tIO._sbInputQueueCharacters); n++)
                {
                    // Insert character if they pass filtering
                    unsigned int c = (unsigned int)gptCtx->tIO._sbInputQueueCharacters[n];
                    if (c == '\t') // Skip Tab, see above.
                        continue;
                    if (pl__input_text_filter_character(&c, tFlags))
                        pl__text_state_on_key_press(ptState, c);
                }

            // consume characters
            plu_sb_reset(gptCtx->tIO._sbInputQueueCharacters)
        }
    }

    // Process other shortcuts/key-presses
    bool bRevertEdit = false;
    if (gptCtx->uActiveId == uHash && !gptCtx->bActiveIdJustActivated && !bClearActiveId)
    {
        PL_UI_ASSERT(ptState != NULL);

        const int iRowCountPerPage = plu_max((int)((tInnerSize.y - gptCtx->tStyle.tFramePadding.y) / gptCtx->tStyle.fFontSize), 1);
        ptState->tStb.row_count_per_page = iRowCountPerPage;

        const int iKMask = (gptCtx->tIO.bKeyShift ? STB_TEXTEDIT_K_SHIFT : 0);
        const bool bIsWordmoveKeyDown = bIsOsX ? gptCtx->tIO.bKeyAlt : gptCtx->tIO.bKeyCtrl;                     // OS X style: Text editing cursor movement using Alt instead of Ctrl
        const bool bIsStartendKeyDown = bIsOsX && gptCtx->tIO.bKeySuper && !gptCtx->tIO.bKeyCtrl && !gptCtx->tIO.bKeyAlt;  // OS X style: Line/Text Start and End using Cmd+Arrows instead of Home/End

        // Using Shortcut() with ImGuiInputFlags_RouteFocused (default policy) to allow routing operations for other code (e.g. calling window trying to use CTRL+A and CTRL+B: formet would be handled by InputText)
        // Otherwise we could simply assume that we own the keys as we are active.
        // const ImGuiInputFlags bRepeat = ImGuiInputFlags_Repeat;
        const bool bRepeat = false;
        const bool bIsCut   = (gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_X, bRepeat)) || (gptCtx->tIO.bKeyShift && pl_is_key_pressed(PL_KEY_DELETE, bRepeat)) && !bIsReadOnly && !bIsPassword && (!bIsMultiLine || pl__text_state_has_selection(ptState));
        const bool bIsCopy  = (gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_C, bRepeat)) || (gptCtx->tIO.bKeyCtrl  && pl_is_key_pressed(PL_KEY_INSERT, bRepeat)) && !bIsPassword && (!bIsMultiLine || pl__text_state_has_selection(ptState));
        const bool bIsPaste = (gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_V, bRepeat)) || ((gptCtx->tIO.bKeyShift && pl_is_key_pressed(PL_KEY_INSERT, bRepeat)) && !bIsReadOnly);
        const bool bIsUndo  = (gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_Z, bRepeat)) && !bIsReadOnly && bIsUndoable;
        const bool bIsRedo =  (gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_Y, bRepeat)) || (bIsOsX && gptCtx->tIO.bKeyShift && gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_Z, bRepeat)) && !bIsReadOnly && bIsUndoable;
        const bool bIsSelectAll = gptCtx->tIO.bKeyCtrl && pl_is_key_pressed(PL_KEY_A, bRepeat);

        // We allow validate/cancel with Nav source (gamepad) to makes it easier to undo an accidental NavInput press with no keyboard wired, but otherwise it isn't very useful.
        const bool bNavGamepadActive = false; // (io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) != 0 && (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;
        const bool bIsEnterPressed = pl_is_key_pressed(PL_KEY_ENTER, true) || pl_is_key_pressed(PL_KEY_KEYPAD_ENTER, true);
        const bool bIsGamepadValidate = false; // nav_gamepad_active && (pl_is_key_pressed(PL_KEY_NavGamepadActivate, false) || pl_is_key_pressed(PL_KEY_NavGamepadInput, false));
        const bool bIsCancel = pl_is_key_pressed(PL_KEY_ESCAPE, bRepeat); // Shortcut(PL_KEY_Escape, id, bRepeat) || (nav_gamepad_active && Shortcut(PL_KEY_NavGamepadCancel, uHash, bRepeat));

        // FIXME: Should use more Shortcut() and reduce pl_is_key_pressed()+SetKeyOwner(), but requires modifiers combination to be taken account of.
        if (pl_is_key_pressed(PL_KEY_LEFT_ARROW, true))                        { pl__text_state_on_key_press(ptState, (bIsStartendKeyDown ? STB_TEXTEDIT_K_LINESTART : bIsWordmoveKeyDown ? STB_TEXTEDIT_K_WORDLEFT : STB_TEXTEDIT_K_LEFT) | iKMask); }
        else if (pl_is_key_pressed(PL_KEY_RIGHT_ARROW, true))                  { pl__text_state_on_key_press(ptState, (bIsStartendKeyDown ? STB_TEXTEDIT_K_LINEEND : bIsWordmoveKeyDown ? STB_TEXTEDIT_K_WORDRIGHT : STB_TEXTEDIT_K_RIGHT) | iKMask); }
        // else if (pl_is_key_pressed(PL_KEY_UP_ARROW, true) && bIsMultiLine)     { if (gptCtx->tIO.bKeyCtrl) SetScrollY(ptDrawWindow, plu_max(ptDrawWindow->tScroll.y - gptCtx->tStyle.fFontSize, 0.0f)); else pl__text_state_on_key_press(ptState, (bIsStartendKeyDown ? STB_TEXTEDIT_K_TEXTSTART : STB_TEXTEDIT_K_UP) | iKMask); }
        // else if (pl_is_key_pressed(PL_KEY_DOWN_ARROW, true) && bIsMultiLine)   { if (gptCtx->tIO.bKeyCtrl) SetScrollY(ptDrawWindow, plu_min(ptDrawWindow->tScroll.y + gptCtx->tStyle.fFontSize, GetScrollMaxY())); else pl__text_state_on_key_press(ptState, (bIsStartendKeyDown ? STB_TEXTEDIT_K_TEXTEND : STB_TEXTEDIT_K_DOWN) | iKMask); }
        // else if (pl_is_key_pressed(PL_KEY_PAGE_UP, true) && bIsMultiLine)      { pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_PGUP | iKMask); fScrollY -= iRowCountPerPage * gptCtx->tStyle.fFontSize; }
        // else if (pl_is_key_pressed(PL_KEY_PAGE_DOWN, true) && bIsMultiLine)    { pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_PGDOWN | iKMask); fScrollY += iRowCountPerPage * gptCtx->tStyle.fFontSize; }
        else if (pl_is_key_pressed(PL_KEY_HOME, true))                        { pl__text_state_on_key_press(ptState,gptCtx->tIO.bKeyCtrl ? STB_TEXTEDIT_K_TEXTSTART | iKMask : STB_TEXTEDIT_K_LINESTART | iKMask); }
        else if (pl_is_key_pressed(PL_KEY_END, true))                         { pl__text_state_on_key_press(ptState,gptCtx->tIO.bKeyCtrl ? STB_TEXTEDIT_K_TEXTEND | iKMask : STB_TEXTEDIT_K_LINEEND | iKMask); }
        else if (pl_is_key_pressed(PL_KEY_DELETE, true) && !bIsReadOnly && !bIsCut)
        {
            if (!pl__text_state_has_selection(ptState))
            {
                // OSX doesn't seem to have Super+Delete to delete until end-of-line, so we don't emulate that (as opposed to Super+Backspace)
                if (bIsWordmoveKeyDown)
                    pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_WORDRIGHT | STB_TEXTEDIT_K_SHIFT);
            }
            pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_DELETE | iKMask);
        }
        else if (pl_is_key_pressed(PL_KEY_BACKSPACE, true) && !bIsReadOnly)
        {
            if (!pl__text_state_has_selection(ptState))
            {
                if (bIsWordmoveKeyDown)
                    pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_WORDLEFT | STB_TEXTEDIT_K_SHIFT);
                else if (bIsOsX && gptCtx->tIO.bKeySuper && !gptCtx->tIO.bKeyAlt && !gptCtx->tIO.bKeyCtrl)
                    pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_LINESTART | STB_TEXTEDIT_K_SHIFT);
            }
            pl__text_state_on_key_press(ptState, STB_TEXTEDIT_K_BACKSPACE | iKMask);
        }
        else if (bIsEnterPressed || bIsGamepadValidate)
        {
            // Determine if we turn Enter into a \n character
            bool bCtrlEnterForNewLine = (tFlags & PL_UI_INPUT_TEXT_FLAGS_CTRL_ENTER_FOR_NEW_LINE) != 0;
            if (!bIsMultiLine || bIsGamepadValidate || (bCtrlEnterForNewLine && !gptCtx->tIO.bKeyCtrl) || (!bCtrlEnterForNewLine && gptCtx->tIO.bKeyCtrl))
            {
                bValidated = true;
                // if (io.ConfigInputTextEnterKeepActive && !bIsMultiLine)
                //     pl__text_state_select_all(ptState); // No need to scroll
                // else
                    bClearActiveId = true;
            }
            else if (!bIsReadOnly)
            {
                unsigned int c = '\n'; // Insert new line
                if (pl__input_text_filter_character(&c, tFlags))
                    pl__text_state_on_key_press(ptState, (int)c);
            }
        }
        else if (bIsCancel)
        {
            if (tFlags & PL_UI_INPUT_TEXT_FLAGS_ESCAPE_CLEARS_ALL)
            {
                if (ptState->iCurrentLengthA > 0)
                {
                    bRevertEdit = true;
                }
                else
                {
                    bRenderCursor = bRenderSelection = false;
                    bClearActiveId = true;
                }
            }
            else
            {
                bClearActiveId = bRevertEdit = true;
                bRenderCursor = bRenderSelection = false;
            }
        }
        else if (bIsUndo || bIsRedo)
        {
            pl__text_state_on_key_press(ptState, bIsUndo ? STB_TEXTEDIT_K_UNDO : STB_TEXTEDIT_K_REDO);
            pl__text_state_clear_selection(ptState);
        }
        else if (bIsSelectAll)
        {
            pl__text_state_select_all(ptState);
            ptState->bCursorFollow = true;
        }
        else if (bIsCut || bIsCopy)
        {
            // Cut, Copy
            if (gptCtx->tIO.set_clipboard_text_fn)
            {
                const int ib = pl__text_state_has_selection(ptState) ? plu_min(ptState->tStb.select_start, ptState->tStb.select_end) : 0;
                const int ie = pl__text_state_has_selection(ptState) ? plu_max(ptState->tStb.select_start, ptState->tStb.select_end) : ptState->iCurrentLengthW;
                const int clipboard_data_len = pl__text_count_utf8_bytes_from_str(ptState->sbTextW + ib, ptState->sbTextW + ie) + 1;
                char* clipboard_data = (char*)pl_memory_alloc(clipboard_data_len * sizeof(char));
                pl__text_str_to_utf8(clipboard_data, clipboard_data_len, ptState->sbTextW + ib, ptState->sbTextW + ie);
                gptCtx->tIO.set_clipboard_text_fn(gptCtx->tIO.pClipboardUserData, clipboard_data);
                pl_memory_free(clipboard_data);
            }
            if (bIsCut)
            {
                if (!pl__text_state_has_selection(ptState))
                    pl__text_state_select_all(ptState);
                ptState->bCursorFollow = true;
                stb_textedit_cut(ptState, &ptState->tStb);
            }
        }
        else if (bIsPaste)
        {
            const char* clipboard = gptCtx->tIO.get_clipboard_text_fn(gptCtx->tIO.pClipboardUserData);
            if (clipboard)
            {
                // Filter pasted buffer
                const int clipboard_len = (int)strlen(clipboard);
                plUiWChar* clipboard_filtered = (plUiWChar*)pl_memory_alloc((clipboard_len + 1) * sizeof(plUiWChar));
                int clipboard_filtered_len = 0;
                for (const char* s = clipboard; *s != 0; )
                {
                    unsigned int c;
                    s += plu_text_char_from_utf8(&c, s, NULL);
                    if (!pl__input_text_filter_character(&c, tFlags))
                        continue;
                    clipboard_filtered[clipboard_filtered_len++] = (plUiWChar)c; //-V522
                }
                clipboard_filtered[clipboard_filtered_len] = 0; //-V522
                if (clipboard_filtered_len > 0) // If everything was filtered, ignore the pasting operation
                {
                    stb_textedit_paste(ptState, &ptState->tStb, clipboard_filtered, clipboard_filtered_len);
                    ptState->bCursorFollow = true;
                }
                pl_memory_free(clipboard_filtered);
            }
        }

        // Update render selection flag after events have been handled, so selection highlight can be displayed during the same frame.
        bRenderSelection |= pl__text_state_has_selection(ptState) && (RENDER_SELECTION_WHEN_INACTIVE || bRenderCursor);
    }

    // Process callbacks and apply result back to user's buffer.
    const char* pcApplyNewText = NULL;
    int iApplyNewTextLength = 0;
    if (gptCtx->uActiveId == uHash)
    {
        PL_UI_ASSERT(ptState != NULL);
        if (bRevertEdit && !bIsReadOnly)
        {
            if (tFlags & PL_UI_INPUT_TEXT_FLAGS_ESCAPE_CLEARS_ALL)
            {
                // Clear input
                pcApplyNewText = "";
                iApplyNewTextLength = 0;
                STB_TEXTEDIT_CHARTYPE empty_string = 0;
                stb_textedit_replace(ptState, &ptState->tStb, &empty_string, 0);
            }
            else if (strcmp(pcBuffer, ptState->sbInitialTextA) != 0)
            {
                // Restore initial value. Only return true if restoring to the initial value changes the current buffer contents.
                // Push records into the undo stack so we can CTRL+Z the revert operation itself
                pcApplyNewText = ptState->sbInitialTextA;
                iApplyNewTextLength = plu_sb_size(ptState->sbInitialTextA) - 1;
                bValueChanged = true;
                plUiWChar* sbWText = NULL;
                if (iApplyNewTextLength > 0)
                {
                    plu_sb_resize(sbWText, pl__text_count_chars_from_utf8(pcApplyNewText, pcApplyNewText + iApplyNewTextLength) + 1);
                    pl__text_str_from_utf8(sbWText, plu_sb_size(sbWText), pcApplyNewText, pcApplyNewText + iApplyNewTextLength, NULL);
                }
                stb_textedit_replace(ptState, &ptState->tStb, sbWText, (iApplyNewTextLength > 0) ? (plu_sb_size(sbWText) - 1) : 0);
            }
        }

        // Apply ASCII value
        if (!bIsReadOnly)
        {
            ptState->bTextAIsValid = true;
            plu_sb_resize(ptState->sbTextA, plu_sb_size(ptState->sbTextW) * 4 + 1);
            pl__text_str_to_utf8(ptState->sbTextA, plu_sb_size(ptState->sbTextA), ptState->sbTextW, NULL);
        }

        // When using 'ImGuiInputTextFlags_EnterReturnsTrue' as a special case we reapply the live buffer back to the input buffer before clearing ActiveId, even though strictly speaking it wasn't modified on this frame.
        // If we didn't do that, code like InputInt() with ImGuiInputTextFlags_EnterReturnsTrue would fail.
        // This also allows the user to use InputText() with ImGuiInputTextFlags_EnterReturnsTrue without maintaining any user-side storage (please note that if you use this property along ImGuiInputTextFlags_CallbackResize you can end up with your temporary string object unnecessarily allocating once a frame, either store your string data, either if you don't then don't use ImGuiInputTextFlags_CallbackResize).
        const bool bApplyEditBackToUserBuffer = !bRevertEdit || (bValidated && (tFlags & PL_UI_INPUT_TEXT_FLAGS_ENTER_RETURNS_TRUE) != 0);
        if (bApplyEditBackToUserBuffer)
        {
            // Apply new value immediately - copy modified buffer back
            // Note that as soon as the input box is active, the in-widget value gets priority over any underlying modification of the input buffer
            // FIXME: We actually always render 'buf' when calling DrawList->AddText, making the comment above incorrect.
            // FIXME-OPT: CPU waste to do this every time the widget is active, should mark dirty state from the stb_textedit callbacks.

            // User callback
            // if ((flags & (ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackAlways)) != 0)
            // {
            //     PL_UI_ASSERT(callback != NULL);

            //     // The reason we specify the usage semantic (Completion/History) is that Completion needs to disable keyboard TABBING at the moment.
            //     ImGuiInputTextFlags event_flag = 0;
            //     ImGuiKey event_key = ImGuiKey_None;
            //     if ((flags & ImGuiInputTextFlags_CallbackCompletion) != 0 && Shortcut(ImGuiKey_Tab, id))
            //     {
            //         event_flag = ImGuiInputTextFlags_CallbackCompletion;
            //         event_key = ImGuiKey_Tab;
            //     }
            //     else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_UpArrow))
            //     {
            //         event_flag = ImGuiInputTextFlags_CallbackHistory;
            //         event_key = ImGuiKey_UpArrow;
            //     }
            //     else if ((flags & ImGuiInputTextFlags_CallbackHistory) != 0 && IsKeyPressed(ImGuiKey_DownArrow))
            //     {
            //         event_flag = ImGuiInputTextFlags_CallbackHistory;
            //         event_key = ImGuiKey_DownArrow;
            //     }
            //     else if ((flags & ImGuiInputTextFlags_CallbackEdit) && state->Edited)
            //     {
            //         event_flag = ImGuiInputTextFlags_CallbackEdit;
            //     }
            //     else if (flags & ImGuiInputTextFlags_CallbackAlways)
            //     {
            //         event_flag = ImGuiInputTextFlags_CallbackAlways;
            //     }

            //     if (event_flag)
            //     {
            //         ImGuiInputTextCallbackData callback_data;
            //         callback_data.Ctx = &g;
            //         callback_data.EventFlag = event_flag;
            //         callback_data.Flags = flags;
            //         callback_data.UserData = callback_user_data;

            //         char* callback_buf = bIsReadOnly ? buf : state->TextA.Data;
            //         callback_data.EventKey = event_key;
            //         callback_data.Buf = callback_buf;
            //         callback_data.BufTextLen = ptState->iCurrentLengthA;
            //         callback_data.BufSize = state->BufCapacityA;
            //         callback_data.BufDirty = false;

            //         // We have to convert from wchar-positions to UTF-8-positions, which can be pretty slow (an incentive to ditch the plUiWChar buffer, see https://github.com/nothings/stb/issues/188)
            //         plUiWChar* text = ptState->sbTextW;
            //         const int utf8_cursor_pos = callback_data.CursorPos = pl__text_count_utf8_bytes_from_str(text, text + ptState->tStb.cursor);
            //         const int utf8_selection_start = callback_data.SelectionStart = pl__text_count_utf8_bytes_from_str(text, text + ptState->tStb.select_start);
            //         const int utf8_selection_end = callback_data.SelectionEnd = pl__text_count_utf8_bytes_from_str(text, text + ptState->tStb.select_end);

            //         // Call user code
            //         callback(&callback_data);

            //         // Read back what user may have modified
            //         callback_buf = bIsReadOnly ? buf : state->TextA.Data; // Pointer may have been invalidated by a resize callback
            //         PL_UI_ASSERT(callback_data.Buf == callback_buf);         // Invalid to modify those fields
            //         PL_UI_ASSERT(callback_data.BufSize == state->BufCapacityA);
            //         PL_UI_ASSERT(callback_data.Flags == flags);
            //         const bool buf_dirty = callback_data.BufDirty;
            //         if (callback_data.CursorPos != utf8_cursor_pos || buf_dirty)            { ptState->tStb.cursor = pl__text_count_chars_from_utf8(callback_data.Buf, callback_data.Buf + callback_data.CursorPos); ptState->bCursorFollow = true; }
            //         if (callback_data.SelectionStart != utf8_selection_start || buf_dirty)  { ptState->tStb.select_start = (callback_data.SelectionStart == callback_data.CursorPos) ? ptState->tStb.cursor : pl__text_count_chars_from_utf8(callback_data.Buf, callback_data.Buf + callback_data.SelectionStart); }
            //         if (callback_data.SelectionEnd != utf8_selection_end || buf_dirty)      { ptState->tStb.select_end = (callback_data.SelectionEnd == callback_data.SelectionStart) ? ptState->tStb.select_start : pl__text_count_chars_from_utf8(callback_data.Buf, callback_data.Buf + callback_data.SelectionEnd); }
            //         if (buf_dirty)
            //         {
            //             PL_UI_ASSERT((flags & ImGuiInputTextFlags_ReadOnly) == 0);
            //             PL_UI_ASSERT(callback_data.BufTextLen == (int)strlen(callback_data.Buf)); // You need to maintain BufTextLen if you change the text!
            //             InputTextReconcileUndoStateAfterUserCallback(state, callback_data.Buf, callback_data.BufTextLen); // FIXME: Move the rest of this block inside function and rename to InputTextReconcileStateAfterUserCallback() ?
            //             if (callback_data.BufTextLen > backup_current_text_length && is_resizable)
            //                 state->TextW.resize(state->TextW.Size + (callback_data.BufTextLen - backup_current_text_length)); // Worse case scenario resize
            //             state->CurLenW = pl__text_str_from_utf8(ptState->sbTextW, state->TextW.Size, callback_data.Buf, NULL);
            //             ptState->iCurrentLengthA = callback_data.BufTextLen;  // Assume correct length and valid UTF-8 from user, saves us an extra strlen()
            //             state->CursorAnimReset();
            //         }
            //     }
            // }

            // Will copy result string if modified
            if (!bIsReadOnly && strcmp(ptState->sbTextA, pcBuffer) != 0)
            {
                pcApplyNewText = ptState->sbTextA;
                iApplyNewTextLength = ptState->iCurrentLengthA;
                bValueChanged = true;
            }
        }
    }

    // Handle reapplying final data on deactivation (see InputTextDeactivateHook() for details)
    // if (g.InputTextDeactivatedState.ID == id)
    // {
    //     if (g.ActiveId != id && IsItemDeactivatedAfterEdit() && !is_readonly)
    //     {
    //         apply_new_text = g.InputTextDeactivatedState.TextA.Data;
    //         apply_new_text_length = g.InputTextDeactivatedState.TextA.Size - 1;
    //         value_changed |= (strcmp(g.InputTextDeactivatedState.TextA.Data, buf) != 0);
    //         //IMGUI_DEBUG_LOG("InputText(): apply Deactivated data for 0x%08X: \"%.*s\".\n", id, apply_new_text_length, apply_new_text);
    //     }
    //     g.InputTextDeactivatedState.ID = 0;
    // }

    // Copy result to user buffer. This can currently only happen when (g.ActiveId == id)
    if (pcApplyNewText != NULL)
    {
        // We cannot test for 'backup_current_text_length != apply_new_text_length' here because we have no guarantee that the size
        // of our owned buffer matches the size of the string object held by the user, and by design we allow InputText() to be used
        // without any storage on user's side.
        PL_UI_ASSERT(iApplyNewTextLength >= 0);
        // if (bIsResizable)
        // {
        //     ImGuiInputTextCallbackData callback_data;
        //     callback_data.Ctx = &g;
        //     callback_data.EventFlag = ImGuiInputTextFlags_CallbackResize;
        //     callback_data.Flags = flags;
        //     callback_data.Buf = buf;
        //     callback_data.BufTextLen = apply_new_text_length;
        //     callback_data.BufSize = plu_max(buf_size, apply_new_text_length + 1);
        //     callback_data.UserData = callback_user_data;
        //     callback(&callback_data);
        //     buf = callback_data.Buf;
        //     buf_size = callback_data.BufSize;
        //     apply_new_text_length = plu_min(callback_data.BufTextLen, buf_size - 1);
        //     PL_UI_ASSERT(apply_new_text_length <= buf_size);
        // }
        //IMGUI_DEBUG_PRINT("InputText(\"%s\"): apply_new_text length %d\n", label, apply_new_text_length);

        // If the underlying buffer resize was denied or not carried to the next frame, apply_new_text_length+1 may be >= buf_size.
        strncpy(pcBuffer, pcApplyNewText, plu_min(iApplyNewTextLength + 1, szBufferSize));
    }

    // Release active ID at the end of the function (so e.g. pressing Return still does a final application of the value)
    // Otherwise request text input ahead for next frame.
    if (gptCtx->uActiveId == uHash && bClearActiveId)
        gptCtx->uNextActiveId = 0;
    else if (gptCtx->uActiveId == uHash)
        gptCtx->tIO.bWantTextInput = true;

    // Render frame
    if (!bIsMultiLine)
    {
        // RenderNavHighlight(frame_bb, id);
        pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgCol);
    }


    // const ImVec4 clip_rect(frame_bb.Min.x, frame_bb.Min.y, frame_bb.Min.x + tInnerSize.x, frame_bb.Min.y + tInnerSize.y); // Not using frame_bb.Max because we have adjusted size
    const plRect clip_rect = {
        .tMin = tBoundingBox.tMin,
        .tMax = {
            .x = tBoundingBox.tMin.x + tInnerSize.x,
            .y = tBoundingBox.tMin.y + tInnerSize.y
        }
    };
    plVec2 draw_pos = bIsMultiLine ? tStartPos : plu_add_vec2(tFrameStartPos, gptCtx->tStyle.tFramePadding);
    plVec2 text_size = {0};

    // Set upper limit of single-line InputTextEx() at 2 million characters strings. The current pathological worst case is a long line
    // without any carriage return, which would makes ImFont::RenderText() reserve too many vertices and probably crash. Avoid it altogether.
    // Note that we only use this limit on single-line InputText(), so a pathologically large line on a InputTextMultiline() would still crash.
    const int iBufferDisplayMaxLength = 2 * 1024 * 1024;
    const char* pcBufferDisplay = bBufferDisplayFromState ? ptState->sbTextA : pcBuffer; //-V595
    const char* pcBufferDisplayEnd = NULL; // We have specialized paths below for setting the length
    if (bIsDisplayingHint)
    {
        pcBufferDisplay = pcHint;
        pcBufferDisplayEnd = pcHint + strlen(pcHint);
    }

    // Render text. We currently only render selection when the widget is active or while scrolling.
    // FIXME: We could remove the '&& bRenderCursor' to keep rendering selection when inactive.
    if (bRenderCursor || bRenderSelection)
    {
        PL_UI_ASSERT(ptState != NULL);
        if (!bIsDisplayingHint)
            pcBufferDisplayEnd = pcBufferDisplay + ptState->iCurrentLengthA;

        // Render text (with cursor and selection)
        // This is going to be messy. We need to:
        // - Display the text (this alone can be more easily clipped)
        // - Handle scrolling, highlight selection, display cursor (those all requires some form of 1d->2d cursor position calculation)
        // - Measure text height (for scrollbar)
        // We are attempting to do most of that in **one main pass** to minimize the computation cost (non-negligible for large amount of text) + 2nd pass for selection rendering (we could merge them by an extra refactoring effort)
        // FIXME: This should occur on pcBufferDisplay but we'd need to maintain cursor/select_start/select_end for UTF-8.
        const plUiWChar* text_begin = ptState->sbTextW;
        plVec2 cursor_offset = {0};
        plVec2 select_start_offset = {0};

        {
            // Find lines numbers straddling 'cursor' (slot 0) and 'select_start' (slot 1) positions.
            const plUiWChar* searches_input_ptr[2] = { NULL, NULL };
            int searches_result_line_no[2] = { -1000, -1000 };
            int searches_remaining = 0;
            if (bRenderCursor)
            {
                searches_input_ptr[0] = text_begin + ptState->tStb.cursor;
                searches_result_line_no[0] = -1;
                searches_remaining++;
            }
            if (bRenderSelection)
            {
                searches_input_ptr[1] = text_begin + plu_min(ptState->tStb.select_start, ptState->tStb.select_end);
                searches_result_line_no[1] = -1;
                searches_remaining++;
            }

            // Iterate all lines to find our line numbers
            // In multi-line mode, we never exit the loop until all lines are counted, so add one extra to the searches_remaining counter.
            searches_remaining += bIsMultiLine ? 1 : 0;
            int line_count = 0;
            //for (const plUiWChar* s = text_begin; (s = (const plUiWChar*)wcschr((const wchar_t*)s, (wchar_t)'\n')) != NULL; s++)  // FIXME-OPT: Could use this when wchar_t are 16-bit
            for (const plUiWChar* s = text_begin; *s != 0; s++)
                if (*s == '\n')
                {
                    line_count++;
                    if (searches_result_line_no[0] == -1 && s >= searches_input_ptr[0]) { searches_result_line_no[0] = line_count; if (--searches_remaining <= 0) break; }
                    if (searches_result_line_no[1] == -1 && s >= searches_input_ptr[1]) { searches_result_line_no[1] = line_count; if (--searches_remaining <= 0) break; }
                }
            line_count++;
            if (searches_result_line_no[0] == -1)
                searches_result_line_no[0] = line_count;
            if (searches_result_line_no[1] == -1)
                searches_result_line_no[1] = line_count;

            // Calculate 2d position by finding the beginning of the line and measuring distance
            cursor_offset.x = pl__input_text_calc_text_size_w(pl__str_bol_w(searches_input_ptr[0], text_begin), searches_input_ptr[0], NULL, NULL, false).x;
            cursor_offset.y = searches_result_line_no[0] * gptCtx->tStyle.fFontSize;
            if (searches_result_line_no[1] >= 0)
            {
                select_start_offset.x = pl__input_text_calc_text_size_w(pl__str_bol_w(searches_input_ptr[1], text_begin), searches_input_ptr[1], NULL, NULL, false).x;
                select_start_offset.y = searches_result_line_no[1] * gptCtx->tStyle.fFontSize;
            }

            // Store text height (note that we haven't calculated text width at all, see GitHub issues #383, #1224)
            if (bIsMultiLine)
                text_size = (plVec2){tInnerSize.x, line_count * gptCtx->tStyle.fFontSize};
        }

        // Scroll
        if (bRenderCursor && ptState->bCursorFollow)
        {
            // Horizontal scroll in chunks of quarter width
            if (!(tFlags & PL_UI_INPUT_TEXT_FLAGS_NO_HORIZONTAL_SCROLL))
            {
                const float scroll_increment_x = tInnerSize.x * 0.25f;
                const float visible_width = tInnerSize.x - gptCtx->tStyle.tFramePadding.x;
                if (cursor_offset.x < ptState->fScrollX)
                    ptState->fScrollX = floorf(plu_max(0.0f, cursor_offset.x - scroll_increment_x));
                else if (cursor_offset.x - visible_width >= ptState->fScrollX)
                    ptState->fScrollX = floorf(cursor_offset.x - visible_width + scroll_increment_x);
            }
            else
            {
                ptState->fScrollX = 0.0f;
            }

            // Vertical scroll
            if (bIsMultiLine)
            {
                // Test if cursor is vertically visible
                if (cursor_offset.y - gptCtx->tStyle.fFontSize < fScrollY)
                    fScrollY = plu_max(0.0f, cursor_offset.y - gptCtx->tStyle.fFontSize);
                else if (cursor_offset.y - (tInnerSize.y - gptCtx->tStyle.tFramePadding.y * 2.0f) >= fScrollY)
                    fScrollY = cursor_offset.y - tInnerSize.y + gptCtx->tStyle.tFramePadding.y * 2.0f;
                const float scroll_max_y = plu_max((text_size.y + gptCtx->tStyle.tFramePadding.y * 2.0f) - tInnerSize.y, 0.0f);
                fScrollY = plu_clampf(0.0f, fScrollY, scroll_max_y);
                draw_pos.y += (ptDrawWindow->tScroll.y - fScrollY);   // Manipulate cursor pos immediately avoid a frame of lag
                ptDrawWindow->tScroll.y = fScrollY;
            }

            ptState->bCursorFollow = false;
        }

        // Draw selection
        const plVec2 draw_scroll = (plVec2){ptState->fScrollX, 0.0f};
        if (bRenderSelection)
        {
            const plUiWChar* text_selected_begin = text_begin + plu_min(ptState->tStb.select_start, ptState->tStb.select_end);
            const plUiWChar* text_selected_end = text_begin + plu_max(ptState->tStb.select_start, ptState->tStb.select_end);

            // ImU32 bg_color = GetColorU32(ImGuiCol_TextSelectedBg, bRenderCursor ? 1.0f : 0.6f); // FIXME: current code flow mandate that bRenderCursor is always true here, we are leaving the transparent one for tests.
            float bg_offy_up = bIsMultiLine ? 0.0f : -1.0f;    // FIXME: those offsets should be part of the style? they don't play so well with multi-line selection.
            float bg_offy_dn = bIsMultiLine ? 0.0f : 2.0f;
            plVec2 rect_pos = plu_sub_vec2(plu_add_vec2(draw_pos, select_start_offset), draw_scroll);
            for (const plUiWChar* p = text_selected_begin; p < text_selected_end; )
            {
                if (rect_pos.y > clip_rect.tMax.y + gptCtx->tStyle.fFontSize)
                    break;
                if (rect_pos.y < clip_rect.tMin.y)
                {
                    //p = (const plUiWChar*)wmemchr((const wchar_t*)p, '\n', text_selected_end - p);  // FIXME-OPT: Could use this when wchar_t are 16-bit
                    //p = p ? p + 1 : text_selected_end;
                    while (p < text_selected_end)
                        if (*p++ == '\n')
                            break;
                }
                else
                {
                    plVec2 rect_size = pl__input_text_calc_text_size_w(p, text_selected_end, &p, NULL, true);
                    if (rect_size.x <= 0.0f) rect_size.x = floorf(gptCtx->ptFont->sbtGlyphs[gptCtx->ptFont->sbuCodePoints[(plUiWChar)' ']].xAdvance * 0.50f); // So we can see selected empty lines
                    plRect rect = {
                        plu_add_vec2(rect_pos, (plVec2){0.0f, bg_offy_up - gptCtx->tStyle.fFontSize}), 
                        plu_add_vec2(rect_pos, (plVec2){rect_size.x, bg_offy_dn})
                    };
                    rect = plu_rect_clip(&rect, &clip_rect);
                    if (plu_rect_overlaps_rect(&rect, &clip_rect))
                        pl_add_rect_filled(ptWindow->ptFgLayer, rect.tMin, rect.tMax, (plVec4){1.0f, 0.0f, 0.0f, 1.0f});
                }
                rect_pos.x = draw_pos.x - draw_scroll.x;
                rect_pos.y += gptCtx->tStyle.fFontSize;
            }
        }

        // We test for 'buf_display_max_length' as a way to avoid some pathological cases (e.g. single-line 1 MB string) which would make ImDrawList crash.
        if (bIsMultiLine || (pcBufferDisplayEnd - pcBufferDisplay) < iBufferDisplayMaxLength)
        {
            // ImU32 col = GetColorU32(bIsDisplayingHint ? ImGuiCol_TextDisabled : ImGuiCol_Text);
            pl_add_text_ex(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, plu_sub_vec2(draw_pos, draw_scroll), gptCtx->tColorScheme.tTextCol, 
                pcBufferDisplay, pcBufferDisplayEnd, 0.0f);
            // draw_window->DrawList->AddText(g.Font, gptCtx->tStyle.fFontSize, draw_pos - draw_scroll, col, pcBufferDisplay, pcBufferDisplayEnd, 0.0f, bIsMultiLine ? NULL : &clip_rect);
        }

        // Draw blinking cursor
        if (bRenderCursor)
        {
            ptState->fCursorAnim += gptCtx->tIO.fDeltaTime;
            // bool cursor_is_visible = (!g.IO.ConfigInputTextCursorBlink) || (ptState->fCursorAnim <= 0.0f) || fmodf(ptState->fCursorAnim, 1.20f) <= 0.80f;
            bool bCursorIsVisible = (ptState->fCursorAnim <= 0.0f) || fmodf(ptState->fCursorAnim, 1.20f) <= 0.80f;
            plVec2 cursor_screen_pos = plu_floor_vec2(plu_sub_vec2(plu_add_vec2(draw_pos, cursor_offset), draw_scroll));
            plRect cursor_screen_rect = {
                {cursor_screen_pos.x, cursor_screen_pos.y - gptCtx->tStyle.fFontSize + 0.5f},
                {cursor_screen_pos.x + 1.0f, cursor_screen_pos.y - 1.5f}
            };
            if (bCursorIsVisible && plu_rect_overlaps_rect(&cursor_screen_rect, &clip_rect))
                pl_add_line(ptWindow->ptFgLayer, cursor_screen_rect.tMin, plu_rect_bottom_left(&cursor_screen_rect), gptCtx->tColorScheme.tTextCol, 1.0f);

            // Notify OS of text input position for advanced IME (-1 x offset so that Windows IME can cover our cursor. Bit of an extra nicety.)
            // if (!bIsReadOnly)
            // {
            //     g.PlatformImeData.WantVisible = true;
            //     g.PlatformImeData.InputPos = plVec2(cursor_screen_pos.x - 1.0f, cursor_screen_pos.y - gptCtx->tStyle.fFontSize);
            //     g.PlatformImeData.InputLineHeight = gptCtx->tStyle.fFontSize;
            // }
        }
    }
    else
    {
        // Render text only (no selection, no cursor)
        if (bIsMultiLine)
            text_size = (plVec2){tInnerSize.x, pl__input_text_calc_text_len_and_line_count(pcBufferDisplay, &pcBufferDisplayEnd) * gptCtx->tStyle.fFontSize}; // We don't need width
        else if (!bIsDisplayingHint && gptCtx->uActiveId == uHash)
            pcBufferDisplayEnd = pcBufferDisplay + ptState->iCurrentLengthA;
        else if (!bIsDisplayingHint)
            pcBufferDisplayEnd = pcBufferDisplay + strlen(pcBufferDisplay);

        if (bIsMultiLine || (pcBufferDisplayEnd - pcBufferDisplay) < iBufferDisplayMaxLength)
        {
            pl_add_text_ex(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, draw_pos, gptCtx->tColorScheme.tTextCol, 
                pcBufferDisplay, pcBufferDisplayEnd, 0.0f);
        }
    }

    // if (bIsPassword && !bIsDisplayingHint)
    //     PopFont();

    if (bIsMultiLine)
    {
        // // For focus requests to work on our multiline we need to ensure our child ItemAdd() call specifies the ImGuiItemFlags_Inputable (ref issue #4761)...
        // Dummy(ImVec2(text_size.x, text_size.y + style.FramePadding.y));
        // ImGuiItemFlags backup_item_flags = g.CurrentItemFlags;
        // g.CurrentItemFlags |= ImGuiItemFlags_Inputable | ImGuiItemFlags_NoTabStop;
        // EndChild();
        // item_data_backup.StatusFlags |= (g.LastItemData.StatusFlags & ImGuiItemStatusFlags_HoveredWindow);
        // g.CurrentItemFlags = backup_item_flags;

        // // ...and then we need to undo the group overriding last item data, which gets a bit messy as EndGroup() tries to forward scrollbar being active...
        // // FIXME: This quite messy/tricky, should attempt to get rid of the child window.
        // EndGroup();
        // if (g.LastItemData.ID == 0)
        // {
        //     g.LastItemData.ID = id;
        //     g.LastItemData.InFlags = item_data_backup.InFlags;
        //     g.LastItemData.StatusFlags = item_data_backup.StatusFlags;
        // }
    }

    // if (pcLabel.x > 0)
    {
        // RenderText(ImVec2(frame_bb.Max.x + style.ItemInnerSpacing.x, frame_bb.Min.y + style.FramePadding.y), label);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tLabelTextActualCenter.y}, gptCtx->tColorScheme.tTextCol, pcLabel, -1.0f);
    }

    // if (value_changed && !(flags & ImGuiInputTextFlags_NoMarkEdited))
    //     MarkItemEdited(id);

    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);

    if ((tFlags & PL_UI_INPUT_TEXT_FLAGS_ENTER_RETURNS_TRUE) != 0)
        return bValidated;
    else
        return bValueChanged;
}

bool
pl_slider_float(const char* pcLabel, float* pfValue, float fMin, float fMax)
{
    return pl_slider_float_f(pcLabel, pfValue, fMin, fMax, "%0.3f");
}

bool
pl_slider_float_f(const char* pcLabel, float* pfValue, float fMin, float fMax, const char* pcFormat)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    const float fOriginalValue = *pfValue;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const plVec2 tFrameStartPos = {floorf(tStartPos.x + (tWidgetSize.x / 3.0f)), tStartPos.y };
        *pfValue = plu_clampf(fMin, *pfValue, fMax);
        const uint32_t uHash = plu_str_hash(pcLabel, 0, plu_sb_top(gptCtx->sbuIdStack));

        char acTextBuffer[64] = {0};
        plu_sprintf(acTextBuffer, pcFormat, *pfValue);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, acTextBuffer, pl_find_renderered_text_end(acTextBuffer, NULL), -1.0f);
        const plRect tLabelTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, pcLabel, pl_find_renderered_text_end(pcLabel, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);
        const plVec2 tLabelTextActualCenter = plu_rect_center(&tLabelTextBounding);

        const plVec2 tSize = { 2.0f * (tWidgetSize.x / 3.0f), tWidgetSize.y};
        const plVec2 tTextStartPos = { 
            tFrameStartPos.x + tFrameStartPos.x + (2.0f * (tWidgetSize.x / 3.0f)) / 2.0f - tTextActualCenter.x, 
            tFrameStartPos.y + tFrameStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        const float fRange = fMax - fMin;
        const float fConv = fRange / (tSize.x - gptCtx->tStyle.fSliderSize);
        
        const plVec2 tGrabStartPos = {
            .x = tFrameStartPos.x + ((*pfValue) - fMin) / fConv,
            .y = tFrameStartPos.y
        };

        const plVec2 tGrabSize = { gptCtx->tStyle.fSliderSize, tWidgetSize.y};
        plRect tGrabBox = plu_calculate_rect(tGrabStartPos, tGrabSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tGrabBox = plu_rect_clip_full(&tGrabBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        const bool bPressed = pl_button_behavior(&tGrabBox, uHash, &bHovered, &bHeld);

        const plRect tBoundingBox = plu_calculate_rect(tFrameStartPos, tSize);
        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgCol);

        pl_add_rect_filled(ptWindow->ptFgLayer, tGrabStartPos, tGrabBox.tMax, gptCtx->tColorScheme.tButtonCol);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tLabelTextActualCenter.y}, gptCtx->tColorScheme.tTextCol, pcLabel, -1.0f);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, acTextBuffer, -1.0f);

        bool bDragged = false;
        if(gptCtx->uActiveId == uHash && pl_is_mouse_dragging(PL_MOUSE_BUTTON_LEFT, 1.0f))
        {
            *pfValue += pl_get_mouse_drag_delta(PL_MOUSE_BUTTON_LEFT, 1.0f).x * fConv;
            *pfValue = plu_clampf(fMin, *pfValue, fMax);
            if(pl_get_mouse_pos().x < tBoundingBox.tMin.x) *pfValue = fMin;
            if(pl_get_mouse_pos().x > tBoundingBox.tMax.x) *pfValue = fMax;
            pl_reset_mouse_drag_delta(PL_MOUSE_BUTTON_LEFT);
        }
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return fOriginalValue != *pfValue;
}

bool
pl_slider_int(const char* pcLabel, int* piValue, int iMin, int iMax)
{
    return pl_slider_int_f(pcLabel, piValue, iMin, iMax, "%d");
}

bool
pl_slider_int_f(const char* pcLabel, int* piValue, int iMin, int iMax, const char* pcFormat)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    const int iOriginalValue = *piValue;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const plVec2 tFrameStartPos = {floorf(tStartPos.x + (tWidgetSize.x / 3.0f)), tStartPos.y };

        *piValue = plu_clampi(iMin, *piValue, iMax);
        const uint32_t uHash = plu_str_hash(pcLabel, 0, plu_sb_top(gptCtx->sbuIdStack));
        const int iBlocks = iMax - iMin + 1;
        const int iBlock = *piValue - iMin;

        char acTextBuffer[64] = {0};
        plu_sprintf(acTextBuffer, pcFormat, *piValue);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, acTextBuffer, pl_find_renderered_text_end(acTextBuffer, NULL), -1.0f);
        const plRect tLabelTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, pcLabel, pl_find_renderered_text_end(pcLabel, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);
        const plVec2 tLabelTextActualCenter = plu_rect_center(&tLabelTextBounding);

        const plVec2 tSize = { 2.0f * (tWidgetSize.x / 3.0f), tWidgetSize.y};
        const plVec2 tTextStartPos = { 
            tFrameStartPos.x + tFrameStartPos.x + (2.0f * (tWidgetSize.x / 3.0f)) / 2.0f - tTextActualCenter.x, 
            tFrameStartPos.y + tFrameStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };
        const float fBlockLength = tSize.x / (float)iBlocks;
        
        const plVec2 tGrabStartPos = {
            .x = tFrameStartPos.x + (float)iBlock * fBlockLength,
            .y = tFrameStartPos.y
        };

        const plVec2 tGrabSize = { fBlockLength, tWidgetSize.y};
        plRect tGrabBox = plu_calculate_rect(tGrabStartPos, tGrabSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tGrabBox = plu_rect_clip_full(&tGrabBox, ptClipRect);
        bool bHovered = false;
        bool bHeld = false;
        const bool bPressed = pl_button_behavior(&tGrabBox, uHash, &bHovered, &bHeld);

        const plRect tBoundingBox = plu_calculate_rect(tFrameStartPos, tSize);
        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgCol);

        pl_add_rect_filled(ptWindow->ptFgLayer, tGrabStartPos, tGrabBox.tMax, gptCtx->tColorScheme.tButtonCol);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tLabelTextActualCenter.y}, gptCtx->tColorScheme.tTextCol, pcLabel, -1.0f);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, acTextBuffer, -1.0f);

        bool bDragged = false;
        if(gptCtx->uActiveId == uHash && pl_is_mouse_dragging(PL_MOUSE_BUTTON_LEFT, 1.0f))
        {
            const plVec2 tMousePos = pl_get_mouse_pos();

            if(tMousePos.x > tGrabBox.tMax.x)
                (*piValue)++;
            if(tMousePos.x < tGrabStartPos.x)
                (*piValue)--;

            *piValue = plu_clampi(iMin, *piValue, iMax);
        }
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return iOriginalValue != *piValue;
}

bool
pl_drag_float(const char* pcLabel, float* pfValue, float fSpeed, float fMin, float fMax)
{
    return pl_drag_float_f(pcLabel, pfValue, fSpeed, fMin, fMax, "%.3f");
}

bool
pl_drag_float_f(const char* pcLabel, float* pfValue, float fSpeed, float fMin, float fMax, const char* pcFormat)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();
    const float fOriginalValue = *pfValue;
    if(pl__ui_should_render(&tStartPos, &tWidgetSize))
    {
        const plVec2 tFrameStartPos = {floorf(tStartPos.x + (tWidgetSize.x / 3.0f)), tStartPos.y };

        *pfValue = plu_clampf(fMin, *pfValue, fMax);
        const uint32_t uHash = plu_str_hash(pcLabel, 0, plu_sb_top(gptCtx->sbuIdStack));

        char acTextBuffer[64] = {0};
        plu_sprintf(acTextBuffer, pcFormat, *pfValue);
        const plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, acTextBuffer, pl_find_renderered_text_end(acTextBuffer, NULL), -1.0f);
        const plRect tLabelTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tFrameStartPos, pcLabel, pl_find_renderered_text_end(pcLabel, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);
        const plVec2 tLabelTextActualCenter = plu_rect_center(&tLabelTextBounding);

        const plVec2 tSize = { 2.0f * (tWidgetSize.x / 3.0f), tWidgetSize.y};
        const plVec2 tTextStartPos = { 
            tFrameStartPos.x + tFrameStartPos.x + (2.0f * (tWidgetSize.x / 3.0f)) / 2.0f - tTextActualCenter.x, 
            tFrameStartPos.y + tFrameStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };
        plRect tBoundingBox = plu_calculate_rect(tFrameStartPos, tSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);

        bool bHovered = false;
        bool bHeld = false;
        const bool bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);

        if(gptCtx->uActiveId == uHash)       pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgActiveCol);
        else if(gptCtx->uHoveredId == uHash) pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgHoveredCol);
        else                                 pl_add_rect_filled(ptWindow->ptFgLayer, tFrameStartPos, tBoundingBox.tMax, gptCtx->tColorScheme.tFrameBgCol);

        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, (plVec2){tStartPos.x, tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tLabelTextActualCenter.y}, gptCtx->tColorScheme.tTextCol, pcLabel, -1.0f);
        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, acTextBuffer, -1.0f);

        bool bDragged = false;
        if(gptCtx->uActiveId == uHash && pl_is_mouse_dragging(PL_MOUSE_BUTTON_LEFT, 1.0f))
        {
            *pfValue = pl_get_mouse_drag_delta(PL_MOUSE_BUTTON_LEFT, 1.0f).x * fSpeed;
            *pfValue = plu_clampf(fMin, *pfValue, fMax);
        }
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
    return fOriginalValue != *pfValue;    
}

void
pl_image(plTextureId tTexture, plVec2 tSize)
{
    pl_image_ex(tTexture, tSize, (plVec2){0}, (plVec2){1.0f, 1.0f}, (plVec4){1.0f, 1.0f, 1.0f, 1.0f}, (plVec4){0});
}

void
pl_image_ex(plTextureId tTexture, plVec2 tSize, plVec2 tUv0, plVec2 tUv1, plVec4 tTintColor, plVec4 tBorderColor)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    const plVec2 tStartPos = pl__ui_get_cursor_pos();

    const plVec2 tFinalPos = plu_add_vec2(tStartPos, tSize);

    if(!(tFinalPos.y < ptWindow->tPos.y || tStartPos.y > ptWindow->tPos.y + ptWindow->tFullSize.y))
    {

        pl_add_image_ex(ptWindow->ptFgLayer, tTexture, tStartPos, tFinalPos, tUv0, tUv1, tTintColor);

        if(tBorderColor.a > 0.0f)
            pl_add_rect(ptWindow->ptFgLayer, tStartPos, tFinalPos, tBorderColor, 1.0f);

    }
    pl_advance_cursor(tSize.x, tSize.y);
}

bool
pl_invisible_button(const char* pcText, plVec2 tSize)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    bool bPressed = false;
    if(!(tStartPos.y + tSize.y < ptWindow->tPos.y || tStartPos.y > ptWindow->tPos.y + ptWindow->tFullSize.y))
    {
        const uint32_t uHash = plu_str_hash(pcText, 0, plu_sb_top(gptCtx->sbuIdStack));
        plRect tBoundingBox = plu_calculate_rect(tStartPos, tSize);
        const plRect* ptClipRect = pl_get_clip_rect(gptCtx->ptDrawlist);
        tBoundingBox = plu_rect_clip_full(&tBoundingBox, ptClipRect);

        bool bHovered = false;
        bool bHeld = false;
        bPressed = pl_button_behavior(&tBoundingBox, uHash, &bHovered, &bHeld);
    }
    pl_advance_cursor(tSize.x, tSize.y);
    return bPressed;  
}

void
pl_dummy(plVec2 tSize)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    pl_advance_cursor(tSize.x, tSize.y);
}

void
pl_progress_bar(float fFraction, plVec2 tSize, const char* pcOverlay)
{
    plUiWindow* ptWindow = gptCtx->ptCurrentWindow;
    plUiLayoutRow* ptCurrentRow = &ptWindow->tTempData.tCurrentLayoutRow;
    const plVec2 tWidgetSize = pl_calculate_item_size(pl_get_frame_height());
    const plVec2 tStartPos   = pl__ui_get_cursor_pos();

    if(tSize.y == 0.0f) tSize.y = tWidgetSize.y;
    if(tSize.x < 0.0f) tSize.x = tWidgetSize.x;

    if(!(tStartPos.y + tSize.y < ptWindow->tPos.y || tStartPos.y > ptWindow->tPos.y + ptWindow->tFullSize.y))
    {

        pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, plu_add_vec2(tStartPos, tSize), gptCtx->tColorScheme.tFrameBgCol);
        pl_add_rect_filled(ptWindow->ptFgLayer, tStartPos, plu_add_vec2(tStartPos, (plVec2){tSize.x * fFraction, tSize.y}), gptCtx->tColorScheme.tProgressBarCol);

        const char* pcTextPtr = pcOverlay;
        
        if(pcOverlay == NULL)
        {
            static char acBuffer[32] = {0};
            plu_sprintf(acBuffer, "%.1f%%", 100.0f * fFraction);
            pcTextPtr = acBuffer;
        }

        const plVec2 tTextSize = pl_ui_calculate_text_size(gptCtx->ptFont, gptCtx->tStyle.fFontSize, pcTextPtr, -1.0f);
        plRect tTextBounding = pl_calculate_text_bb_ex(gptCtx->ptFont, gptCtx->tStyle.fFontSize, tStartPos, pcTextPtr, pl_find_renderered_text_end(pcTextPtr, NULL), -1.0f);
        const plVec2 tTextActualCenter = plu_rect_center(&tTextBounding);

        plVec2 tTextStartPos = {
            .x = tStartPos.x + gptCtx->tStyle.tInnerSpacing.x + gptCtx->tStyle.tFramePadding.x + tSize.x * fFraction,
            .y = tStartPos.y + tStartPos.y + tWidgetSize.y / 2.0f - tTextActualCenter.y
        };

        if(tTextStartPos.x + tTextSize.x > tStartPos.x + tSize.x)
            tTextStartPos.x = tStartPos.x + tSize.x - tTextSize.x - gptCtx->tStyle.tInnerSpacing.x;

        pl_ui_add_text(ptWindow->ptFgLayer, gptCtx->ptFont, gptCtx->tStyle.fFontSize, tTextStartPos, gptCtx->tColorScheme.tTextCol, pcTextPtr, -1.0f);

        const bool bHovered = pl_is_mouse_hovering_rect(tStartPos, plu_add_vec2(tStartPos, tWidgetSize)) && ptWindow == gptCtx->ptHoveredWindow;
        gptCtx->tPrevItemData.bHovered = bHovered;
    }
    pl_advance_cursor(tWidgetSize.x, tWidgetSize.y);
}

//-----------------------------------------------------------------------------
// [SECTION] internal api implementation
//-----------------------------------------------------------------------------

static bool
pl__input_text_filter_character(unsigned int* puChar, plUiInputTextFlags tFlags)
{
    //PL_UI_ASSERT(input_source == ImGuiInputSource_Keyboard || input_source == ImGuiInputSource_Clipboard);
    unsigned int c = *puChar;

    // Filter non-printable (NB: isprint is unreliable! see #2467)
    bool apply_named_filters = true;
    if (c < 0x20)
    {
        bool pass = false;
        pass |= (c == '\n' && (tFlags & PL_UI_INPUT_TEXT_FLAGS_MULTILINE)); // Note that an Enter KEY will emit \r and be ignored (we poll for KEY in InputText() code)
        pass |= (c == '\t' && (tFlags & PL_UI_INPUT_TEXT_FLAGS_ALLOW_TAB_INPUT));
        if (!pass)
            return false;
        apply_named_filters = false; // Override named filters below so newline and tabs can still be inserted.
    }

    // if (input_source != ImGuiInputSource_Clipboard)
    {
        // We ignore Ascii representation of delete (emitted from Backspace on OSX, see #2578, #2817)
        if (c == 127)
            return false;

        // Filter private Unicode range. GLFW on OSX seems to send private characters for special keys like arrow keys (FIXME)
        if (c >= 0xE000 && c <= 0xF8FF)
            return false;
    }

    // Filter Unicode ranges we are not handling in this build
    if (c > PL_UNICODE_CODEPOINT_MAX)
        return false;

    // Generic named filters
    if (apply_named_filters && (tFlags & (PL_UI_INPUT_TEXT_FLAGS_CHARS_DECIMAL | PL_UI_INPUT_TEXT_FLAGS_CHARS_HEXADECIMAL | PL_UI_INPUT_TEXT_FLAGS_CHARS_UPPERCASE | PL_UI_INPUT_TEXT_FLAGS_CHARS_NO_BLANK | PL_UI_INPUT_TEXT_FLAGS_CHARS_SCIENTIFIC)))
    {
        // The libc allows overriding locale, with e.g. 'setlocale(LC_NUMERIC, "de_DE.UTF-8");' which affect the output/input of printf/scanf to use e.g. ',' instead of '.'.
        // The standard mandate that programs starts in the "C" locale where the decimal point is '.'.
        // We don't really intend to provide widespread support for it, but out of empathy for people stuck with using odd API, we support the bare minimum aka overriding the decimal point.
        // Change the default decimal_point with:
        //   ImGui::GetCurrentContext()->PlatformLocaleDecimalPoint = *localeconv()->decimal_point;
        // Users of non-default decimal point (in particular ',') may be affected by word-selection logic (pl__is_word_boundary_from_right/pl__is_word_boundary_from_left) functions.
        const unsigned c_decimal_point = '.';

        // Full-width -> half-width conversion for numeric fields (https://en.wikipedia.org/wiki/Halfwidth_and_Fullwidth_Forms_(Unicode_block)
        // While this is mostly convenient, this has the side-effect for uninformed users accidentally inputting full-width characters that they may
        // scratch their head as to why it works in numerical fields vs in generic text fields it would require support in the font.
        if (tFlags & (PL_UI_INPUT_TEXT_FLAGS_CHARS_DECIMAL | PL_UI_INPUT_TEXT_FLAGS_CHARS_SCIENTIFIC | PL_UI_INPUT_TEXT_FLAGS_CHARS_HEXADECIMAL))
            if (c >= 0xFF01 && c <= 0xFF5E)
                c = c - 0xFF01 + 0x21;

        // Allow 0-9 . - + * /
        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CHARS_DECIMAL)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/'))
                return false;

        // Allow 0-9 . - + * / e E
        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CHARS_SCIENTIFIC)
            if (!(c >= '0' && c <= '9') && (c != c_decimal_point) && (c != '-') && (c != '+') && (c != '*') && (c != '/') && (c != 'e') && (c != 'E'))
                return false;

        // Allow 0-9 a-F A-F
        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CHARS_HEXADECIMAL)
            if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F'))
                return false;

        // Turn a-z into A-Z
        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CHARS_UPPERCASE)
            if (c >= 'a' && c <= 'z')
                c += (unsigned int)('A' - 'a');

        if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CHARS_NO_BLANK)
            if (pl__char_is_blank_w(c))
                return false;

        *puChar = c;
    }

    // Custom callback filter
    if (tFlags & PL_UI_INPUT_TEXT_FLAGS_CALLBACK_CHAR_FILTER)
    {
        // ImGuiContext& g = *GImGui;
        // ImGuiInputTextCallbackData callback_data;
        // callback_data.Ctx = &g;
        // callback_data.EventFlag = ImGuiInputTextFlags_CallbackCharFilter;
        // callback_data.EventChar = (ImWchar)c;
        // callback_data.Flags = tFlags;
        // callback_data.UserData = user_data;
        // if (callback(&callback_data) != 0)
        //     return false;
        // *p_char = callback_data.EventChar;
        // if (!callback_data.EventChar)
        //     return false;
    }

    return true;
}