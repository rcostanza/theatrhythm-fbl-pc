/* The set number above the party in the song setup window, at 4K. The label is a UnityEngine.UI.Text
 * 35 units tall with a 26 font and vertical overflow "Truncate": Text drops any line that does not fit,
 * and with render_resolution=4k (canvas scale 2) the generator returns no lines at all, so the window
 * says "SET" with no number. Before updatePartySet sets the text, a stub turns that label's vertical
 * overflow to "Overflow"; it stays centred in its box, so at 1080p nothing moves.
 * The site's bytes are checked first, so another game version keeps its own code. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define LABEL_SET_TEXT_CALL 0x1687146 /* <updatePartySet>d__30::MoveNext: _partyNumberText.SetText(term) */
#define LABEL_SET_TEXT 0x392f1d0      /* LocalizedTextBase::SetText(this, term) */
#define TEXT_GET_VERTICAL_OVERFLOW 0x33f2e20

/* UnityEngine.UI.Text::get_verticalOverflow: mov rax,[rdi+m_FontData]; mov eax,[rax+m_VerticalOverflow] */
static const unsigned char get_vertical_overflow[]={0x48,0x8b,0x87,0xd0,0x00,0x00,0x00, 0x8b,0x40,0x38};

int runtime_party_label_install(unsigned char *image, uint64_t module_base, unsigned char *stubs, size_t size) {
    static const unsigned char set_overflow[]={
        0x48,0x8b,0x87,0xd0,0x00,0x00,0x00,       /* mov rax,[rdi+m_FontData] */
        0xc7,0x40,0x38,0x01,0x00,0x00,0x00,       /* mov dword [rax+m_VerticalOverflow],1 (Overflow) */
    };
    unsigned char *module=image+module_base, *site=module+LABEL_SET_TEXT_CALL, *target=module+LABEL_SET_TEXT;
    int32_t rel=(int32_t)(target-(site+5));
    if (site[0]!=0xe8 || memcmp(site+1,&rel,4) ||
        memcmp(module+TEXT_GET_VERTICAL_OVERFLOW,get_vertical_overflow,sizeof get_vertical_overflow) ||
        size<sizeof set_overflow+5) {
        printf("Party label: game code differs at 0x%llx; the set number stays blank at 4K\n",(unsigned long long)LABEL_SET_TEXT_CALL);
        return -1;
    }
    memcpy(stubs,set_overflow,sizeof set_overflow);
    unsigned char *jump=stubs+sizeof set_overflow;
    rel=(int32_t)(target-(jump+5));
    jump[0]=0xe9; /* jmp LocalizedTextBase::SetText */
    memcpy(jump+1,&rel,4);
    rel=(int32_t)(stubs-(site+5));
    memcpy(site+1,&rel,4);
    printf("Party label: the song setup window's set number may overflow its box\n");
    return 0;
}
