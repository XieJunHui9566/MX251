typedef unsigned char u8;typedef signed char s8;typedef unsigned int u16;typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
u32 dense8(u32 a,u32 b) ABI {u32 v;switch((u8)a){
case 0:v=b+0x01020304UL;break;
case 1:v=b+0x02040608UL;break;
case 2:v=b+0x0306090cUL;break;
case 3:v=b+0x04080c10UL;break;
case 4:v=b+0x050a0f14UL;break;
case 5:v=b+0x060c1218UL;break;
case 6:v=b+0x070e151cUL;break;
case 7:v=b+0x08101820UL;break;
default:v=b^0xdeadbeefUL;break;}return v^(a+0x11111111UL);}
u32 gapped8(u32 a,u32 b) ABI {u32 v;switch((u8)a){
case 0:v=b+0x01020304UL;break;
case 1:v=b+0x02040608UL;break;
case 3:v=b+0x0306090cUL;break;
case 4:v=b+0x04080c10UL;break;
case 6:v=b+0x050a0f14UL;break;
case 7:v=b+0x060c1218UL;break;
case 8:v=b+0x070e151cUL;break;
case 10:v=b+0x08101820UL;break;
default:v=b^0xdeadbeefUL;break;}return v^(a+0x11111111UL);}
u32 signed8(u32 a,u32 b) ABI {u32 v;switch((s8)a){
case -4:v=b+0x01020304UL;break;
case -3:v=b+0x02040608UL;break;
case -2:v=b+0x0306090cUL;break;
case -1:v=b+0x04080c10UL;break;
case 0:v=b+0x050a0f14UL;break;
case 1:v=b+0x060c1218UL;break;
case 2:v=b+0x070e151cUL;break;
case 3:v=b+0x08101820UL;break;
case 4:v=b+0x09121b24UL;break;
default:v=b^0xdeadbeefUL;break;}return v^(a+0x11111111UL);}
u32 nonzero16(u32 a,u32 b) ABI {u32 v;switch((u16)a){
case 1000:v=b+0x01020304UL;break;
case 1001:v=b+0x02040608UL;break;
case 1002:v=b+0x0306090cUL;break;
case 1003:v=b+0x04080c10UL;break;
case 1004:v=b+0x050a0f14UL;break;
case 1005:v=b+0x060c1218UL;break;
case 1006:v=b+0x070e151cUL;break;
case 1007:v=b+0x08101820UL;break;
case 1008:v=b+0x09121b24UL;break;
case 1009:v=b+0x0a141e28UL;break;
case 1010:v=b+0x0b16212cUL;break;
case 1011:v=b+0x0c182430UL;break;
default:v=b^0xdeadbeefUL;break;}return v^(a+0x11111111UL);}
u32 wide32(u32 a,u32 b) ABI {u32 v;switch(a){
case 305397760UL:v=b+0x01020304UL;break;
case 305397761UL:v=b+0x02040608UL;break;
case 305397762UL:v=b+0x0306090cUL;break;
case 305397763UL:v=b+0x04080c10UL;break;
case 305397764UL:v=b+0x050a0f14UL;break;
case 305397765UL:v=b+0x060c1218UL;break;
case 305397766UL:v=b+0x070e151cUL;break;
case 305397767UL:v=b+0x08101820UL;break;
case 305397768UL:v=b+0x09121b24UL;break;
case 305397769UL:v=b+0x0a141e28UL;break;
case 305397770UL:v=b+0x0b16212cUL;break;
case 305397771UL:v=b+0x0c182430UL;break;
default:v=b^0xdeadbeefUL;break;}return v^(a+0x11111111UL);}
u32 full256(u32 a,u32 b) ABI {u32 v;switch((u8)a){
case 0:case 1:case 2:case 3:case 4:case 5:case 6:case 7:case 8:case 9:case 10:case 11:case 12:case 13:case 14:case 15:v=b^0x01020304UL;break;
case 16:case 17:case 18:case 19:case 20:case 21:case 22:case 23:case 24:case 25:case 26:case 27:case 28:case 29:case 30:case 31:v=b^0x02040608UL;break;
case 32:case 33:case 34:case 35:case 36:case 37:case 38:case 39:case 40:case 41:case 42:case 43:case 44:case 45:case 46:case 47:v=b^0x0306090cUL;break;
case 48:case 49:case 50:case 51:case 52:case 53:case 54:case 55:case 56:case 57:case 58:case 59:case 60:case 61:case 62:case 63:v=b^0x04080c10UL;break;
case 64:case 65:case 66:case 67:case 68:case 69:case 70:case 71:case 72:case 73:case 74:case 75:case 76:case 77:case 78:case 79:v=b^0x050a0f14UL;break;
case 80:case 81:case 82:case 83:case 84:case 85:case 86:case 87:case 88:case 89:case 90:case 91:case 92:case 93:case 94:case 95:v=b^0x060c1218UL;break;
case 96:case 97:case 98:case 99:case 100:case 101:case 102:case 103:case 104:case 105:case 106:case 107:case 108:case 109:case 110:case 111:v=b^0x070e151cUL;break;
case 112:case 113:case 114:case 115:case 116:case 117:case 118:case 119:case 120:case 121:case 122:case 123:case 124:case 125:case 126:case 127:v=b^0x08101820UL;break;
case 128:case 129:case 130:case 131:case 132:case 133:case 134:case 135:case 136:case 137:case 138:case 139:case 140:case 141:case 142:case 143:v=b^0x09121b24UL;break;
case 144:case 145:case 146:case 147:case 148:case 149:case 150:case 151:case 152:case 153:case 154:case 155:case 156:case 157:case 158:case 159:v=b^0x0a141e28UL;break;
case 160:case 161:case 162:case 163:case 164:case 165:case 166:case 167:case 168:case 169:case 170:case 171:case 172:case 173:case 174:case 175:v=b^0x0b16212cUL;break;
case 176:case 177:case 178:case 179:case 180:case 181:case 182:case 183:case 184:case 185:case 186:case 187:case 188:case 189:case 190:case 191:v=b^0x0c182430UL;break;
case 192:case 193:case 194:case 195:case 196:case 197:case 198:case 199:case 200:case 201:case 202:case 203:case 204:case 205:case 206:case 207:v=b^0x0d1a2734UL;break;
case 208:case 209:case 210:case 211:case 212:case 213:case 214:case 215:case 216:case 217:case 218:case 219:case 220:case 221:case 222:case 223:v=b^0x0e1c2a38UL;break;
case 224:case 225:case 226:case 227:case 228:case 229:case 230:case 231:case 232:case 233:case 234:case 235:case 236:case 237:case 238:case 239:v=b^0x0f1e2d3cUL;break;
case 240:case 241:case 242:case 243:case 244:case 245:case 246:case 247:case 248:case 249:case 250:case 251:case 252:case 253:case 254:case 255:v=b^0x10203040UL;break;
}return v+a;}
u32 nested_switch(u32 a,u32 b) ABI {u32 v;switch((u8)(a&7UL)){
case 0:v=b+17UL;break;
case 1:v=b+34UL;break;
case 2:v=b+51UL;break;
case 3:v=b+68UL;break;
case 4:v=b+85UL;break;
case 5:v=b+102UL;break;
case 6:v=b+119UL;break;
case 7:v=b+136UL;break;
}switch((u8)((a>>8)&7UL)){
case 0:v^=0x01020304UL;break;
case 1:v^=0x02040608UL;break;
case 2:v^=0x0306090cUL;break;
case 3:v^=0x04080c10UL;break;
case 4:v^=0x050a0f14UL;break;
case 5:v^=0x060c1218UL;break;
case 6:v^=0x070e151cUL;break;
case 7:v^=0x08101820UL;break;
}return v+a;}
u32 pressure_switch(u32 a,u32 b) ABI {u32 v0=a+1UL,v1=a+2UL,v2=a+3UL,v3=a+4UL,v4=a+5UL,v5=a+6UL,v6=a+7UL,v7=a+8UL;u32 v;switch((u8)(b&7UL)){
case 0:v=v0^b;break;
case 1:v=v1^b;break;
case 2:v=v2^b;break;
case 3:v=v3^b;break;
case 4:v=v4^b;break;
case 5:v=v5^b;break;
case 6:v=v6^b;break;
case 7:v=v7^b;break;
}return (v+v0+v1+v2+v3+v4+v5+v6+v7)^a;}
u32 volatile_switch(u32 a,u32 b) ABI {volatile u32 t[2];t[0]=a;t[1]=b;switch((u8)(a&7UL)){
case 0:t[0]+=0x01020304UL;break;
case 1:t[1]+=0x02040608UL;break;
case 2:t[0]+=0x0306090cUL;break;
case 3:t[1]+=0x04080c10UL;break;
case 4:t[0]+=0x050a0f14UL;break;
case 5:t[1]+=0x060c1218UL;break;
case 6:t[0]+=0x070e151cUL;break;
case 7:t[1]+=0x08101820UL;break;
}return t[0]^t[1];}
u32 calls_switch(u32 a,u32 b) ABI {u32 v;switch((u8)(a&7UL)){
case 0:v=dense8(b+0UL,a);break;
case 1:v=dense8(b+1UL,a);break;
case 2:v=dense8(b+2UL,a);break;
case 3:v=dense8(b+3UL,a);break;
case 4:v=dense8(b+4UL,a);break;
case 5:v=dense8(b+5UL,a);break;
case 6:v=dense8(b+6UL,a);break;
case 7:v=dense8(b+7UL,a);break;
}return v+b;}
