typedef unsigned char u8;
typedef unsigned int u16;
typedef struct { u8 tag; u16 size; u8 *ptr; u8 last; } State;
extern __data State state;
extern __code u8 table[8];
u8 read_tag(void) { return state.tag; }
u8 *read_pointer(void) { return state.ptr; }
void write_pointer(u8 *p) { state.ptr=p; }
void write_code(void) { state.ptr=table+2; }
