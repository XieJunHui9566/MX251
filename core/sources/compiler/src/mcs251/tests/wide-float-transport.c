typedef unsigned char u8;
typedef unsigned long u32;
#define ABI __reentrant __sdcccall(4)
/* 这里只移动 binary32 位模式；NaN payload 和负零也不能被改写。 */
float identity(float a) ABI {return a;}
float first(float a,float b) ABI {return a;}
float second(float a,float b) ABI {return b;}
float swap(float a,float b) ABI {return second(b,a);}
float retained(float a,float b) ABI {float c=first(b,a);return second(c,a);}
float join(float a,u32 b) ABI {if(b&1UL)return identity(a);return -2.5f;}
float select_integer(u32 a,float b) ABI {return a?b:1.25f;}
float literal_pos(float a,float b) ABI {return second(a,1.25f);}
float literal_neg(float a,float b) ABI {return first(-2.5f,b);}
float literal_zero(float a,float b) ABI {return first(0.0f,b);}
float literal_negzero(float a,float b) ABI {return first(-0.0f,b);}
float literal_subnormal(float a,float b) ABI {return second(a,1.40129846432481707092372958328991613e-45f);}
float literal_max(float a,float b) ABI {return first(3.40282346638528859811704183484516925e38f,b);}
/* 算术仍由已经验证的运行库执行；这里验证带类型的参数、返回和活值保存。 */
float add(float a,float b) ABI {return a+b;}
float sub(float a,float b) ABI {return a-b;}
float mul(float a,float b) ABI {return a*b;}
float chained(float a,float b) ABI {float c=a+b;float d=a-b;return c*d;}
float polynomial(float a,float b) ABI {return ((a*0.125f+1.25f)*a-2.5f)*a+b;}
float pressure(float a,float b) ABI {
 float c=first(a,b),d=first(b,a),e=second(a,b),f=second(b,a);
 float g=first(c,d),h=second(e,f);
 return (c+d)+(e+f)+(g+h);
}
