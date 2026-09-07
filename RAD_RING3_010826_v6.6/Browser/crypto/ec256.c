#include "ec256.h"
#include "../system/string.h"

typedef struct { uint32_t l[8]; } b256;   // limbs little-endian

static const b256 P  = {{0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0x00000000,0x00000000,0x00000000,0x00000001,0xFFFFFFFF}};
static const b256 N  = {{0xFC632551,0xF3B9CAC2,0xA7179E84,0xBCE6FAAD,0xFFFFFFFF,0xFFFFFFFF,0x00000000,0xFFFFFFFF}};
static const b256 GX = {{0xD898C296,0xF4A13945,0x2DEB33A0,0x77037D81,0x63A440F2,0xF8BCE6E5,0xE12C4247,0x6B17D1F2}};
static const b256 GY = {{0x37BF51F5,0xCBB64068,0x6B315ECE,0x2BCE3357,0x7C0F9E16,0x8EE7EB4A,0xFE1A7F9B,0x4FE342E2}};

static int  bn_is_zero(const b256* a){ for(int i=0;i<8;i++) if(a->l[i]) return 0; return 1; }

static int  bn_cmp(const b256* a, const b256* b){ for(int i=7;i>=0;i--){ if(a->l[i]<b->l[i])return -1; if(a->l[i]>b->l[i])return 1;} return 0; }

static void bn_from_be(b256* a, const uint8_t be[32]){
    for(int i=0;i<8;i++){ const uint8_t* p=be+(28-4*i);
        a->l[i]=((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
}
static uint32_t bn_add(b256* r,const b256* a,const b256* b){ uint64_t c=0;
    for(int i=0;i<8;i++){ c+=(uint64_t)a->l[i]+b->l[i]; r->l[i]=(uint32_t)c; c>>=32; } return (uint32_t)c; }

static uint32_t bn_sub(b256* r,const b256* a,const b256* b){ int64_t c=0;
    for(int i=0;i<8;i++){ c+=(int64_t)a->l[i]-b->l[i]; r->l[i]=(uint32_t)c; c>>=32; } return (uint32_t)(c&1); }

static void bn_mul512(uint32_t out[16], const b256* a, const b256* b){
    __int128 acc[16]; for(int i=0;i<16;i++) acc[i]=0;
    for(int i=0;i<8;i++) for(int j=0;j<8;j++) acc[i+j]+=(__int128)a->l[i]*b->l[j];
    __int128 c=0;
    for(int i=0;i<16;i++){ acc[i]+=c; out[i]=(uint32_t)acc[i]; c=acc[i]>>32; }
}
// redução genérica bit-a-bit (produto 512b mod m) — simples e à prova de bug
static void bn_mod512(b256* r, const uint32_t p512[16], const b256* m){
    b256 acc; for(int i=0;i<8;i++) acc.l[i]=0;
    for(int w=15;w>=0;w--) for(int bb=31;bb>=0;bb--){
        uint32_t carry=0;
        for(int i=0;i<8;i++){ uint32_t nc=acc.l[i]>>31; acc.l[i]=(acc.l[i]<<1)|carry; carry=nc; }
        if((p512[w]>>bb)&1) acc.l[0]|=1;
        if(carry || bn_cmp(&acc,m)>=0){ b256 t; bn_sub(&t,&acc,m); acc=t; }
    }
    *r=acc;
}

static void mod_mul(b256* r,const b256* a,const b256* b,const b256* m){
    uint32_t p[16]; bn_mul512(p,a,b); bn_mod512(r,p,m);
}

static void mod_add(b256* r,const b256* a,const b256* b,const b256* m){
    b256 t; uint32_t c=bn_add(&t,a,b);
    if(c||bn_cmp(&t,m)>=0){ b256 u; bn_sub(&u,&t,m); *r=u; } else *r=t;
}

static void mod_sub(b256* r,const b256* a,const b256* b,const b256* m){
    if(bn_cmp(a,b)>=0){ bn_sub(r,a,b); } else { b256 t,u; bn_sub(&t,b,a); bn_sub(&u,m,&t); *r=u; }
}

static void b256_init(b256* r, uint32_t val){
    for(int i=0;i<8;i++) r->l[i]=0;
    r->l[0]=val;
}

static void mod_inv(b256* r,const b256* a,const b256* m){   // Fermat: a^(m-2)
    b256 two, e, res, base;
    b256_init(&two, 2);
    b256_init(&res, 1);
    base = *a;
    bn_sub(&e,m,&two);
    for(int i=0;i<8;i++) for(int bb=0;bb<32;bb++){
        if((e.l[i]>>bb)&1) mod_mul(&res,&res,&base,m);
        mod_mul(&base,&base,&base,m);
    }
    *r=res;
}

/* ---------- pontos P-256 em coordenadas Jacobianas (a=-3) ---------- */
typedef struct { b256 X,Y,Z; int inf; } jpt;

static void jdouble(jpt* r, const jpt* a){
    if(a->inf||bn_is_zero(&a->Y)){ r->inf=1; return; }
    b256 A,B,C,D,E,F,t1,t2,t3;
    mod_mul(&A,&a->X,&a->X,&P); mod_mul(&B,&a->Y,&a->Y,&P); mod_mul(&C,&B,&B,&P);
    mod_add(&t1,&a->X,&B,&P); mod_mul(&t1,&t1,&t1,&P);
    mod_sub(&t1,&t1,&A,&P); mod_sub(&t1,&t1,&C,&P); mod_add(&D,&t1,&t1,&P);
    mod_add(&t2,&A,&A,&P); mod_add(&t2,&t2,&A,&P);
    mod_mul(&t3,&a->Z,&a->Z,&P); mod_mul(&t3,&t3,&t3,&P);
    mod_sub(&E,&t2,&t3,&P); mod_sub(&E,&E,&t3,&P); mod_sub(&E,&E,&t3,&P);  // 3A-3Z^4
    mod_mul(&F,&E,&E,&P);
    mod_sub(&r->X,&F,&D,&P); mod_sub(&r->X,&r->X,&D,&P);
    mod_add(&t1,&a->Y,&a->Y,&P); mod_mul(&r->Z,&t1,&a->Z,&P);
    mod_sub(&t2,&D,&r->X,&P); mod_mul(&t2,&t2,&E,&P);
    mod_add(&t3,&C,&C,&P); mod_add(&t3,&t3,&t3,&P); mod_add(&t3,&t3,&t3,&P); // 8C
    mod_sub(&r->Y,&t2,&t3,&P);
    r->inf=0;
}
static void jadd(jpt* r, const jpt* a, const jpt* b){
    if(a->inf){ *r=*b; return; } if(b->inf){ *r=*a; return; }
    b256 Z1Z1,Z2Z2,U1,U2,S1,S2,H,I,J,rr,V,t1,t2;
    mod_mul(&Z1Z1,&a->Z,&a->Z,&P); mod_mul(&Z2Z2,&b->Z,&b->Z,&P);
    mod_mul(&U1,&a->X,&Z2Z2,&P);   mod_mul(&U2,&b->X,&Z1Z1,&P);
    mod_mul(&t1,&b->Z,&Z2Z2,&P);   mod_mul(&S1,&a->Y,&t1,&P);
    mod_mul(&t2,&a->Z,&Z1Z1,&P);   mod_mul(&S2,&b->Y,&t2,&P);
    if(bn_cmp(&U1,&U2)==0){
        if(bn_cmp(&S1,&S2)!=0){ r->inf=1; return; }
        jdouble(r,a); return;
    }
    mod_sub(&H,&U2,&U1,&P);
    mod_add(&I,&H,&H,&P); mod_mul(&I,&I,&I,&P);
    mod_mul(&J,&H,&I,&P);
    mod_sub(&rr,&S2,&S1,&P); mod_add(&rr,&rr,&rr,&P);
    mod_mul(&V,&U1,&I,&P);
    mod_mul(&r->X,&rr,&rr,&P); mod_sub(&r->X,&r->X,&J,&P);
    mod_sub(&r->X,&r->X,&V,&P); mod_sub(&r->X,&r->X,&V,&P);
    mod_sub(&t1,&V,&r->X,&P); mod_mul(&t1,&t1,&rr,&P);
    mod_mul(&t2,&S1,&J,&P); mod_add(&t2,&t2,&t2,&P);
    mod_sub(&r->Y,&t1,&t2,&P);
    mod_add(&t1,&a->Z,&b->Z,&P); mod_mul(&t1,&t1,&t1,&P);
    mod_sub(&t1,&t1,&Z1Z1,&P); mod_sub(&t1,&t1,&Z2Z2,&P);
    mod_mul(&r->Z,&t1,&H,&P);
    r->inf=0;
}
static void jmul(jpt* r, const b256* k, const jpt* a){
    jpt acc; acc.inf=1;
    for(int i=255;i>=0;i--){
        jdouble(&acc,&acc);
        if((k->l[i/32]>>(i&31))&1) jadd(&acc,&acc,a);
    }
    *r=acc;
}
static void to_affine(b256* x, b256* y, const jpt* a){
    b256 zi, z2, z3;
    mod_inv(&zi,&a->Z,&P);
    mod_mul(&z2,&zi,&zi,&P); mod_mul(&z3,&z2,&zi,&P);
    mod_mul(x,&a->X,&z2,&P); mod_mul(y,&a->Y,&z3,&P);
}

int ecdsa_p256_verify(const uint8_t hash[32], const uint8_t r_be[32], const uint8_t s_be[32],
                      const uint8_t qx_be[32], const uint8_t qy_be[32]){
    b256 r,s,qx,qy,e;
    bn_from_be(&r,r_be); bn_from_be(&s,s_be); bn_from_be(&qx,qx_be); bn_from_be(&qy,qy_be); bn_from_be(&e,hash);
    if(bn_is_zero(&r)||bn_is_zero(&s)) return -1;
    if(bn_cmp(&r,&N)>=0||bn_cmp(&s,&N)>=0) return -1;
    // pubkey na curva? y^2 == x^3-3x+b
    b256 l,rr2;
    mod_mul(&l,&qy,&qy,&P);
    mod_mul(&rr2,&qx,&qx,&P); mod_sub(&rr2,&rr2,&(b256){{3}},&P); mod_mul(&rr2,&rr2,&qx,&P);
    mod_add(&rr2,&rr2,&(b256){{0x27D2604B,0x3BCE3C3E,0xCC53B0F6,0x651D06B0,0x769886BC,0xB3EBBD55,0xAA3A93E7,0x5AC635D8}},&P);
    if(bn_cmp(&l,&rr2)!=0) return -4;
    b256 w,u1,u2;
    mod_inv(&w,&s,&N);
    mod_mul(&u1,&e,&w,&N); mod_mul(&u2,&r,&w,&N);
    jpt G,Q,R1,R2,R;
    static const b256 ONE = {{1}}; 
    G.X=GX; G.Y=GY; G.Z=ONE; G.inf=0;
    Q.X=qx; Q.Y=qy; Q.Z=ONE; Q.inf=0;
    jmul(&R1,&u1,&G); jmul(&R2,&u2,&Q);
    jadd(&R,&R1,&R2);
    if(R.inf) return -2;
    b256 x1,y1; to_affine(&x1,&y1,&R);
    if(bn_cmp(&x1,&N)>=0){ b256 t; bn_sub(&t,&x1,&N); x1=t; }
    return (bn_cmp(&x1,&r)==0) ? 0 : -3;
}

int ecdsa_parse_der_sig(const uint8_t* der, int len, uint8_t r[32], uint8_t s[32]){
    int i=0;
    if(len<8||der[i++]!=0x30) return -1;
    i++;                                   // len total (forma curta)
    if(der[i++]!=0x02) return -2;
    int rl=der[i++]; const uint8_t* rp=der+i; i+=rl;
    if(i>=len||der[i++]!=0x02) return -3;
    int sl=der[i++]; const uint8_t* sp=der+i;
    for(int k=0;k<32;k++){ r[k]=0; s[k]=0; }
    while(rl>1&&rp[0]==0){rp++;rl--;}
    while(sl>1&&sp[0]==0){sp++;sl--;}
    if(rl>32||sl>32) return -4;
    for(int k=0;k<rl;k++) r[32-rl+k]=rp[k];
    for(int k=0;k<sl;k++) s[32-sl+k]=sp[k];
    return 0;
}
