/* Fifteen party sets instead of five. The game has no one constant for the count, so each use changes:
 *  - PartyInfo's constructor and save reader end in a hook that extends PartySets to fifteen, with a
 *    default PartySet in each new slot (a save writes whatever length the array has);
 *  - MusicStageInfo._partyNumber is a RangedInt(0,4): its range becomes (0,14);
 *  - Edit Party and the song setup window step with Mathf.Repeat(n, 5.0f). The 5.0f is shared with
 *    unrelated code, so those instructions read a 15.0f from the stub page instead;
 *  - entering Edit Party loads the art of sets 0-4 by index: a hook loads the others;
 *  - Edit Party's "did anything change" check snapshots and compares sets 0-4: the snapshot grows and
 *    the comparison also covers sets 5-14, or edits to them would never reach the save;
 *  - the prefabs hold five page dots and window illustrations, and nothing checks these indexes: both
 *    screens show set % 5 on them (the "SET n" text has the real number);
 *  - the set's number is the localized term "Common/SetNo/<n>", which exists up to 5: above that its
 *    translation is term 1's with the real number in place of the digit;
 *  - a multiplayer room sends the first five sets and the active index into them: it stays below five.
 * Every site's bytes are checked first; another game version keeps its five sets. */
#include "runtime.h"
#include <stdio.h>
#include <string.h>

#define PARTY_COUNT 15
#define ARRAY_COUNT 0x18
#define ARRAY_ITEMS 0x20

#define ARRAY_NEW 0x456ab0
#define OBJECT_NEW 0x4545a0
#define PARTY_ARRAY_CLASS 0x6097380
#define PARTY_SET_CLASS 0x60a2240
#define PARTY_SET_CTOR 0x10598f0
#define PARTY_SET_COPY_CTOR 0x105a040
#define BACKUP_CLASS 0x6097c70       /* its static field 0 is the BackupData */
#define RESERVE_CLASS 0x609c268      /* PartyEditReserveSaveManager; static field 0 is _beforePartySets */
#define ASSET_LOADER_CLASS 0x609c230 /* PartyEditAssetLoadManager; static field 0 is the instance */
#define ASSET_LOAD_PARTY 0x1118d50   /* LoadAsync(PartySet) */
#define RESERVE_EQUALS 0x233af50     /* equelsPartySets(a, b): compares items 0-4 of two PartySet[] */

#define PARTY_INFO_CTOR_END 0x10598c5
#define PARTY_INFO_ARRAY_NEW 0x1059600
#define PARTY_INFO_READER_END 0x19d3ab4
#define PARTY_NUMBER_RANGE 0x45da6a5
#define RESERVE_ARRAY_NEW 0x233bcc3
#define RESERVE_SNAPSHOT_END 0x233ac33
#define RESERVE_EQUALS_CALL 0x233af0f
#define PRELOAD_END 0x1132eef
#define PRELOAD_NEXT_SLOT 0x609e3d0  /* what PRELOAD_END's mov rdi,[rip+...] reads */
#define SETUP_LIGHT 0x16871fb        /* updatePartySet, Indicator::SwitchLight inlined: mov rbx,lights[rax] */
#define MULTI_ACTIVE_RANGE 0x166897d /* DummyRoomData::AddMemberOwn: the range of ActivePartyIndex */
#define GET_TRANSLATION 0x1e63cf0    /* TermInfo::GetTranslation(this): every translated text's string */

#define REPEAT_FIVE 0x4616e64        /* the game's 5.0f */
static const uint32_t repeat_sites[]={
    0x1137bbb,0x1137bdf,                     /* Edit Party, L1/R1 */
    0x16880b6,0x16880eb,0x168811f,0x168812d, /* PartyListBehaviour::OnSetFocus */
    0x168859f,0x16885c7,0x16885ff,0x168860d, /* PartyListBehaviour::OnChangeParty */
    0x168af2f,0x168af61,0x168af95,0x168afa3, /* WindowSetupBodyPartsMain::OnSwitchFocus */
};
/* Edit Party's UpdatePartySet: mov ebx,[r13+_selectedPartyIndex], compared with 0-4 for each dot and
 * illustration. The real index is read separately for the party and the text. */
static const uint32_t page_sites[]={
    0x23389ef,0x2338a36,0x2338a7d,0x2338ac5,0x2338b0d,0x2338b55,0x2338b9d,0x2338be5,0x2338c2d,0x2338c75,
};
static const unsigned char page_site[]={0x41,0x8b,0x9d,0xe8,0x00,0x00,0x00};

static const unsigned char array_new_five[]={0xbe,0x05,0x00,0x00,0x00};              /* mov esi,5 */
static const unsigned char end_8_rbx[]={0x48,0x83,0xc4,0x08,0x5b};                   /* add rsp,8; pop rbx */
static const unsigned char reader_end[]={0x48,0x8b,0x45,0x98,0x48,0x83,0xc4,0x58};   /* mov rax,[rbp-0x68]; add rsp,0x58 */
static const unsigned char range_0_4[]={0x48,0xb9,0,0,0,0,4,0,0,0};                  /* mov rcx,(max 4, min 0) */
static const unsigned char preload_end[]={0x48,0x8b,0x3d,0xda,0xb4,0xf6,0x04};
static const unsigned char setup_light[]={0x49,0x8b,0x5c,0xc4,0x20};                 /* mov rbx,[r12+8*rax+0x20] */
static const unsigned char translation_entry[]={0x55,0x48,0x89,0xe5,0x41,0x57};     /* push rbp; mov rbp,rsp; push r15 */
static const unsigned char multi_range[]={0x8b,0x72,0x10,0x8b,0x52,0x14};            /* mov esi,[rdx+0x10]; mov edx,[rdx+0x14] */

static unsigned char *module;
static void *(ABI *get_translation)(void *, void *); /* the game's, through its first instructions in a stub */

static void *read_ptr(const void *at) { void *v; memcpy(&v,at,sizeof v); return v; }
static void write_ptr(void *at, void *v) { memcpy(at,&v,sizeof v); }
static void *class_static0(uint64_t klass) { return read_ptr(read_ptr((unsigned char *)read_ptr(module+klass)+0xb8)); }
static void *new_object(void) { return ((void *(ABI *)(void *))(module+OBJECT_NEW))(read_ptr(module+PARTY_SET_CLASS)); }
static unsigned char *current_party_sets(void) {
    const unsigned char *data=class_static0(BACKUP_CLASS);
    return read_ptr((unsigned char *)read_ptr(data+0x58)+0x20); /* BackupData.PartyInfo.PartySets */
}

/* PartyInfo.PartySets (+0x20) with fewer than fifteen sets: a longer array with the same sets first. */
static void ABI extend_party_sets(unsigned char *info) {
    unsigned char *old=info ? read_ptr(info+0x20) : NULL;
    int32_t count;
    if (!old) return;
    memcpy(&count,old+ARRAY_COUNT,sizeof count);
    if (count>=PARTY_COUNT) return;
    unsigned char *sets=((void *(ABI *)(void *, int32_t))(module+ARRAY_NEW))(read_ptr(module+PARTY_ARRAY_CLASS),PARTY_COUNT);
    memcpy(sets+ARRAY_ITEMS,old+ARRAY_ITEMS,8*(size_t)count);
    for (int32_t i=count;i<PARTY_COUNT;++i) {
        void *set=new_object();
        ((void (ABI *)(void *))(module+PARTY_SET_CTOR))(set);
        write_ptr(sets+ARRAY_ITEMS+8*i,set);
    }
    write_ptr(info+0x20,sets);
}

/* The end of UpdateBeforePartyData, which copied sets 0-4 to _beforePartySets: the same for the rest. */
static void ABI snapshot_other_sets(void) {
    unsigned char *before=class_static0(RESERVE_CLASS), *sets=current_party_sets();
    for (int i=5;i<PARTY_COUNT;++i) {
        void *copy=new_object();
        ((void (ABI *)(void *, void *, void *))(module+PARTY_SET_COPY_CTOR))(copy,read_ptr(sets+ARRAY_ITEMS+8*i),NULL);
        write_ptr(before+ARRAY_ITEMS+8*i,copy);
    }
}

/* equelsPartySets for fifteen: it reads only items 0-4 at fixed offsets, so it runs on each group of five. */
static unsigned char ABI party_sets_equal(unsigned char *before, unsigned char *sets) {
    for (int i=0;i<PARTY_COUNT;i+=5)
        if (!((unsigned char (ABI *)(void *, void *))(module+RESERVE_EQUALS))(before+8*i,sets+8*i)) return 0;
    return 1;
}

/* PartyEditMain's setup loaded the current set, then each of sets 0-4 that is not current. */
static void ABI load_other_sets(void) {
    const unsigned char *data=class_static0(BACKUP_CLASS), *sets=current_party_sets();
    int32_t current;
    memcpy(&current,(unsigned char *)read_ptr((unsigned char *)read_ptr(data+0x30)+0x20)+0x18,sizeof current);
    void *loader=class_static0(ASSET_LOADER_CLASS);
    for (int i=5;i<PARTY_COUNT;++i)
        if (i!=current) ((void (ABI *)(void *, void *))(module+ASSET_LOAD_PARTY))(loader,read_ptr(sets+ARRAY_ITEMS+8*i));
}

/* A guest System.String (length at +0x10, UTF-16 from +0x14) with these characters, of the class of the
 * guest string `like`: one per text, in low memory outside the game's heap, so it is never freed. */
static void *number_string(const unsigned char *like, const uint16_t *chars, int32_t length) {
    static struct { uint16_t chars[32]; int32_t length; unsigned char *string; } made[160];
    static unsigned count;
    for (unsigned i=0;i<count;++i)
        if (made[i].length==length && !memcmp(made[i].chars,chars,2*(size_t)length)) return made[i].string;
    if (count==sizeof made/sizeof *made) return NULL;
    unsigned char *s=runtime_low_alloc(0x16+2*(size_t)length);
    if (!s) return NULL;
    memset(s,0,0x14);
    memcpy(s,like,8);
    memcpy(s+0x10,&length,4);
    memcpy(s+0x14,chars,2*(size_t)length);
    memcpy(made[count].chars,chars,2*(size_t)length);
    made[count].length=length; made[count].string=s;
    return made[count++].string;
}

/* The <n> of a guest string "Common/SetNo/<n>", or 0. */
static int set_number_of(const unsigned char *term) {
    static const char prefix[]="Common/SetNo/";
    const int32_t prefix_length=(int32_t)sizeof prefix-1;
    int32_t length;
    if (!term) return 0;
    memcpy(&length,term+0x10,4);
    if (length<=prefix_length || length>prefix_length+2) return 0;
    int number=0;
    for (int32_t i=0;i<length;++i) {
        uint16_t c;
        memcpy(&c,term+0x14+2*i,2);
        if (i<prefix_length) { if (c!=(uint8_t)prefix[i]) return 0; }
        else if (c<'0' || c>'9') return 0;
        else number=number*10+(c-'0');
    }
    return number;
}

/* TermInfo::GetTranslation(this) (the term at +0x10). A set's number above 5 has no term: it is term 1's
 * translation with its first run of digits (ASCII or full width, as the language writes them) replaced,
 * so a text that translates again, on being enabled or on a language change, gets the same. */
static void *ABI translation(unsigned char *info, void *method) {
    const unsigned char *term=read_ptr(info+0x10);
    const int number=set_number_of(term);
    if (number<=5) return get_translation(info,method);

    int32_t length;
    memcpy(&length,term+0x10,4);
    uint16_t chars[16];
    memcpy(chars,term+0x14,2*(size_t)length);
    while (chars[length-1]>='0' && chars[length-1]<='9') --length;
    chars[length++]='1';
    void *first=number_string(term,chars,length);
    if (!first) return get_translation(info,method);
    write_ptr(info+0x10,first);
    const unsigned char *shown=get_translation(info,method);
    write_ptr(info+0x10,(void *)term);

    uint16_t out[32];
    int32_t size=0, shown_length=0;
    if (shown) memcpy(&shown_length,shown+0x10,4);
    int replaced=0;
    for (int32_t i=0;i<shown_length;) {
        uint16_t c;
        memcpy(&c,shown+0x14+2*i,2);
        const uint16_t zero=c>=0xff10 && c<=0xff19 ? 0xff10 : c>='0' && c<='9' ? '0' : 0;
        if (zero && !replaced) {
            if (size+2>(int32_t)(sizeof out/sizeof *out)) break;
            if (number>9) out[size++]=(uint16_t)(zero+number/10);
            out[size++]=(uint16_t)(zero+number%10);
            replaced=1;
            for (;i<shown_length;++i) { /* the rest of this run */
                memcpy(&c,shown+0x14+2*i,2);
                if (!((c>=0xff10 && c<=0xff19) || (c>='0' && c<='9'))) break;
            }
            continue;
        }
        if (size==(int32_t)(sizeof out/sizeof *out)) break;
        out[size++]=c;
        ++i;
    }
    if (!replaced) { /* no digit to take the place of: the number alone */
        size=0;
        if (number>9) out[size++]=(uint16_t)('0'+number/10);
        out[size++]=(uint16_t)('0'+number%10);
    }
    void *string=number_string(term,out,size);
    return string ? string : (void *)shown;
}

static unsigned char *emit(unsigned char *p, const void *bytes, size_t size) { memcpy(p,bytes,size); return p+size; }
#define EMIT(p,...) emit(p,(const unsigned char[]){__VA_ARGS__},sizeof((const unsigned char[]){__VA_ARGS__}))
static unsigned char *emit_call(unsigned char *p, const void *function) { /* mov rax,function; call rax */
    const uint64_t address=(uint64_t)(uintptr_t)function;
    p=EMIT(p,0x48,0xb8); p=emit(p,&address,8);
    return EMIT(p,0xff,0xd0);
}
static unsigned char *emit_jump_back(unsigned char *p, const void *target) { /* jmp [rip] */
    const uint64_t address=(uint64_t)(uintptr_t)target;
    p=EMIT(p,0xff,0x25,0,0,0,0);
    return emit(p,&address,8);
}
/* `length` bytes at `site` become a jmp or call (opcode) to `target`; the rest are never run. */
static void write_branch(unsigned char *site, size_t length, unsigned char opcode, const unsigned char *target) {
    const int32_t rel=(int32_t)(target-(site+5));
    site[0]=opcode; memcpy(site+1,&rel,4);
    memset(site+5,0x90,length-5);
}
#define SITE_IS(offset,bytes) (!memcmp(module+(offset),bytes,sizeof bytes))
static int calls(uint64_t site, uint64_t target) { /* call rel32 */
    int32_t rel;
    memcpy(&rel,module+site+1,4);
    return module[site]==0xe8 && site+5+(int64_t)rel==target;
}

int runtime_party_limit_install(unsigned char *image, uint64_t module_base, unsigned char *stubs, size_t size) {
    enum { STUB_CTOR=0, STUB_READER=64, STUB_PAGE=128, STUB_FIFTEEN=192, STUB_LIGHT=200, STUB_SNAPSHOT=256, STUB_EQUAL=320, STUB_PRELOAD=384, STUB_NUMBER=448, STUB_TRANSLATION=464, STUB_END=488 };
    module=image+module_base;
    int same=size>=STUB_END && SITE_IS(PARTY_INFO_CTOR_END,end_8_rbx) && SITE_IS(PARTY_INFO_ARRAY_NEW,array_new_five) &&
             SITE_IS(PARTY_INFO_READER_END,reader_end) && SITE_IS(PARTY_NUMBER_RANGE,range_0_4) &&
             SITE_IS(RESERVE_ARRAY_NEW,array_new_five) && SITE_IS(RESERVE_SNAPSHOT_END,end_8_rbx) &&
             SITE_IS(PRELOAD_END,preload_end) && SITE_IS(SETUP_LIGHT,setup_light) && SITE_IS(MULTI_ACTIVE_RANGE,multi_range) &&
             calls(RESERVE_EQUALS_CALL,RESERVE_EQUALS) && SITE_IS(GET_TRANSLATION,translation_entry);
    for (size_t i=0;same && i<sizeof repeat_sites/sizeof *repeat_sites;++i) {
        int32_t rel;
        memcpy(&rel,module+repeat_sites[i]+4,4);
        same=repeat_sites[i]+8+(int64_t)rel==REPEAT_FIVE; /* vmovss/vucomiss xmm,[rip+rel] */
    }
    for (size_t i=0;same && i<sizeof page_sites/sizeof *page_sites;++i) same=SITE_IS(page_sites[i],page_site);
    if (!same) {
        printf("Party sets: game code differs; it keeps five\n");
        return -1;
    }

    /* PartyInfo's constructor and save reader; both then return as they did. */
    unsigned char *p=stubs+STUB_CTOR;
    p=EMIT(p,0x4c,0x89,0xf7);                                  /* mov rdi,r14 */
    p=emit_call(p,extend_party_sets);
    p=emit(p,end_8_rbx,sizeof end_8_rbx);
    emit_jump_back(p,module+PARTY_INFO_CTOR_END+sizeof end_8_rbx);
    write_branch(module+PARTY_INFO_CTOR_END,sizeof end_8_rbx,0xe9,stubs+STUB_CTOR);

    p=stubs+STUB_READER;
    p=emit(p,reader_end,sizeof reader_end);
    p=EMIT(p,0x50,0x48,0x89,0xc7);                             /* push rax; mov rdi,rax */
    p=emit_call(p,extend_party_sets);
    p=EMIT(p,0x58);                                            /* pop rax */
    emit_jump_back(p,module+PARTY_INFO_READER_END+sizeof reader_end);
    write_branch(module+PARTY_INFO_READER_END,sizeof reader_end,0xe9,stubs+STUB_READER);

    module[PARTY_NUMBER_RANGE+6]=PARTY_COUNT-1;

    const float fifteen=PARTY_COUNT;
    memcpy(stubs+STUB_FIFTEEN,&fifteen,4);
    for (size_t i=0;i<sizeof repeat_sites/sizeof *repeat_sites;++i) {
        unsigned char *site=module+repeat_sites[i];
        const int32_t rel=(int32_t)(stubs+STUB_FIFTEEN-(site+8));
        memcpy(site+4,&rel,4);
    }

    /* The dot and illustration index: ebx = _selectedPartyIndex % 5, every other register kept. */
    p=stubs+STUB_PAGE;
    p=EMIT(p,0x50,0x51,0x52, 0x41,0x8b,0x85,0xe8,0,0,0, 0x31,0xd2, 0xb9,5,0,0,0, 0xf7,0xf1, 0x89,0xd3, 0x5a,0x59,0x58, 0xc3);
    for (size_t i=0;i<sizeof page_sites/sizeof *page_sites;++i)
        write_branch(module+page_sites[i],sizeof page_site,0xe8,stubs+STUB_PAGE);

    /* The setup window's light: rbx = _onLights[rax % Length]; the next instruction overwrites rax.
     * push rdx; xor edx,edx; div dword [r12+0x18]; mov rbx,[r12+8*rdx+0x20]; pop rdx; ret */
    p=stubs+STUB_LIGHT;
    p=EMIT(p,0x52, 0x31,0xd2, 0x41,0xf7,0x74,0x24,ARRAY_COUNT, 0x49,0x8b,0x5c,0xd4,ARRAY_ITEMS, 0x5a, 0xc3);
    write_branch(module+SETUP_LIGHT,sizeof setup_light,0xe8,stubs+STUB_LIGHT);

    /* The change check. */
    module[RESERVE_ARRAY_NEW+1]=PARTY_COUNT;
    p=stubs+STUB_SNAPSHOT;
    p=emit_call(p,snapshot_other_sets);
    p=emit(p,end_8_rbx,sizeof end_8_rbx);
    emit_jump_back(p,module+RESERVE_SNAPSHOT_END+sizeof end_8_rbx);
    write_branch(module+RESERVE_SNAPSHOT_END,sizeof end_8_rbx,0xe9,stubs+STUB_SNAPSHOT);
    p=stubs+STUB_EQUAL;
    emit_jump_back(p,(const void *)party_sets_equal);
    write_branch(module+RESERVE_EQUALS_CALL,5,0xe8,stubs+STUB_EQUAL);

    /* The art of the sets past the first five. */
    p=stubs+STUB_PRELOAD;
    p=emit_call(p,load_other_sets);
    const uint64_t next_slot=(uint64_t)(uintptr_t)(module+PRELOAD_NEXT_SLOT);
    p=EMIT(p,0x48,0xbf); p=emit(p,&next_slot,8);               /* mov rdi,slot */
    p=EMIT(p,0x48,0x8b,0x3f);                                  /* mov rdi,[rdi] */
    emit_jump_back(p,module+PRELOAD_END+sizeof preload_end);
    write_branch(module+PRELOAD_END,sizeof preload_end,0xe9,stubs+STUB_PRELOAD);

    /* The set's number: GetTranslation enters translation(), which runs the game's through this stub. */
    p=stubs+STUB_TRANSLATION;
    p=emit(p,translation_entry,sizeof translation_entry);
    emit_jump_back(p,module+GET_TRANSLATION+sizeof translation_entry);
    get_translation=(void *(ABI *)(void *, void *))(void *)(stubs+STUB_TRANSLATION);
    emit_jump_back(stubs+STUB_NUMBER,(const void *)translation);
    write_branch(module+GET_TRANSLATION,sizeof translation_entry,0xe9,stubs+STUB_NUMBER);

    /* xor esi,esi; push 4; pop rdx; nop: clamp ActivePartyIndex to 0-4. */
    memcpy(module+MULTI_ACTIVE_RANGE,(const unsigned char[]){0x31,0xf6,0x6a,0x04,0x5a,0x90},6);

    printf("Party sets: fifteen instead of five\n");
    return 0;
}
