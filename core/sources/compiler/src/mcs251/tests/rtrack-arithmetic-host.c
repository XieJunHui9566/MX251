/* Exercise the actual tracker with compiler-service stubs; no MCU emulator. */
#include "../rtrack.c"

struct options options;
reg_info mcs251_regs[END_IDX];
void *Safe_malloc(size_t n) { void *p=malloc(n); assert(p); return p; }
void *Safe_alloc(size_t n) { void *p=calloc(1,n); assert(p); return p; }
void Safe_free(void *p) { free(p); }
char *Safe_strdup(const char *s) {
    char *p=Safe_malloc(strlen(s)+1); strcpy(p,s); return p;
}
void emitcode(const char *inst,const char *fmt,...) { abort(); }
void fatal(int exitCode,int errNum,...) { abort(); }
int mcs251_regname_to_idx(const char *name) { return -1; }
int mcs251_regIdxForNumber(unsigned n) {
    static const int indices[]={R0_IDX,R1_IDX,R2_IDX,R3_IDX,R4_IDX,R5_IDX,R6_IDX,R7_IDX,
        R8_IDX,R9_IDX,R10_IDX,R11_IDX,R12_IDX,R13_IDX,R14_IDX,R15_IDX};
    assert(n<16); return indices[n];
}

int main(void)
{
    unsigned cases=0;
    for(unsigned a=0;a<256;++a) for(unsigned b=0;b<256;++b) {
        rtrack_data_set_val(A_IDX,a); rtrack_data_set_val(B_IDX,b);
        assert(!_mcs251_rtrackUpdate("div\tab"));
        if(b) {
            assert(mcs251_regs[A_IDX].rtrack.valueKnown && mcs251_regs[B_IDX].rtrack.valueKnown);
            unsigned q=mcs251_regs[A_IDX].rtrack.value, r=mcs251_regs[B_IDX].rtrack.value;
            assert(q*b+r==a && r<b);
        } else {
            assert(!mcs251_regs[A_IDX].rtrack.valueKnown && !mcs251_regs[B_IDX].rtrack.valueKnown);
        }
        ++cases;
        rtrack_data_set_val(A_IDX,a); rtrack_data_set_val(B_IDX,b);
        assert(!_mcs251_rtrackUpdate("mul\tab"));
        assert(mcs251_regs[A_IDX].rtrack.valueKnown && mcs251_regs[B_IDX].rtrack.valueKnown);
        assert(mcs251_regs[A_IDX].rtrack.value+256u*mcs251_regs[B_IDX].rtrack.value==a*b);
        ++cases;
    }
    const char *native[]={"div\twr6,wr8","div\twr16,wr20","mul\twr4,wr8","mul\ta,r7"};
    for(unsigned i=0;i<sizeof(native)/sizeof(native[0]);++i) {
        for(unsigned r=0;r<16;++r) rtrack_data_set_val(mcs251_regIdxForNumber(r),0x5a);
        rtrack_data_set_val(A_IDX,23); rtrack_data_set_val(B_IDX,0);
        assert(!_mcs251_rtrackUpdate(native[i]));
        for(unsigned r=0;r<16;++r) assert(!mcs251_regs[mcs251_regIdxForNumber(r)].rtrack.valueKnown);
        assert(!mcs251_regs[A_IDX].rtrack.valueKnown && !mcs251_regs[B_IDX].rtrack.valueKnown);
        ++cases;
    }
    printf("%u\n",cases);
    return 0;
}
