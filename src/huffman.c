/* TODO 4/5: Huffman encoder and decoder.
 * Format: HFS1, mode, original size, region offset, region size,
 *         leaf count (all 32-bit little endian), then (symbol, frequency)
 *         pairs, raw bytes outside the region, then MSB-first coded bits.
 * Both peers must use this same format.
 */
#include "textlink.h"

typedef struct {
    uint32_t symbol, frequency;
    int left, right, parent;
} Node;

static uint32_t read32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 |
           (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void write32(uint8_t *p, uint32_t x) {
    p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8);
    p[2]=(uint8_t)(x>>16); p[3]=(uint8_t)(x>>24);
}
static unsigned read16(const uint8_t *p) {
    return p[0] | (unsigned)p[1]<<8;
}

/* Find PCM16 samples. Preserve the header, other chunks and odd last byte. */
static int region(const uint8_t *p, size_t n, int mode,
                  size_t *start, size_t *length) {
    size_t pos=12;
    int fmt=0, data=0;
    *start=0; *length=n;
    if (mode==SYM_BYTE) return TL_OK;
    if (mode==SYM_CHAR) return utf8_validate(p,n);
    if (mode!=SYM_S16 || n<12 || memcmp(p,"RIFF",4) ||
        memcmp(p+8,"WAVE",4) || read32(p+4)!=n-8) return TL_ERR_DATA;
    while (pos<n) {
        size_t size, step;
        if (n-pos<8) return TL_ERR_DATA;
        size=read32(p+pos+4);
        if (size>n-pos-8) return TL_ERR_DATA;
        if (!memcmp(p+pos,"fmt ",4)) {
            if (size<16 || read16(p+pos+8)!=1 ||
                !read16(p+pos+10) || read16(p+pos+22)!=16)
                return TL_ERR_DATA;
            fmt=1;
        }
        if (!memcmp(p+pos,"data",4)) {
            if (data) return TL_ERR_DATA;
            *start=pos+8; *length=size & ~(size_t)1; data=1;
        }
        step=8+size+(size&1);
        if (step>n-pos) return TL_ERR_DATA;
        pos+=step;
    }
    return fmt && data ? TL_OK : TL_ERR_DATA;
}

/* Input was validated before this function is called. */
static uint32_t next_symbol(const uint8_t *p, size_t *pos, int mode) {
    uint32_t x=p[(*pos)++];
    int extra=0;
    if (mode==SYM_S16) return x | (uint32_t)p[(*pos)++]<<8;
    if (mode==SYM_CHAR && x>=128) {
        if (x<224) { x&=31; extra=1; }
        else if (x<240) { x&=15; extra=2; }
        else { x&=7; extra=3; }
        while (extra--) x=(x<<6) | (p[(*pos)++]&63);
    }
    return x;
}

/* Min heap: frequency first, node index breaks ties consistently. */
static int less(const Node *nodes, int a, int b) {
    return nodes[a].frequency<nodes[b].frequency ||
        (nodes[a].frequency==nodes[b].frequency && a<b);
}
static void push(int *heap, int *count, Node *nodes, int x) {
    int i=(*count)++;
    while (i && less(nodes,x,heap[(i-1)/2])) {
        heap[i]=heap[(i-1)/2]; i=(i-1)/2;
    }
    heap[i]=x;
}
static int pop(int *heap, int *count, Node *nodes) {
    int result=heap[0], x=heap[--(*count)], i=0;
    while (2*i+1<*count) {
        int child=2*i+1;
        if (child+1<*count && less(nodes,heap[child+1],heap[child])) child++;
        if (!less(nodes,heap[child],x)) break;
        heap[i]=heap[child]; i=child;
    }
    if (*count) heap[i]=x;
    return result;
}
static int build(Node *nodes, int leaves, int *heap) {
    int count=0, used=leaves;
    for (int i=0;i<leaves;i++) {
        nodes[i].left=nodes[i].right=nodes[i].parent=-1;
        push(heap,&count,nodes,i);
    }
    while (count>1) {
        int a=pop(heap,&count,nodes), b=pop(heap,&count,nodes);
        nodes[used].frequency=nodes[a].frequency+nodes[b].frequency;
        nodes[used].left=a; nodes[used].right=b; nodes[used].parent=-1;
        nodes[a].parent=nodes[b].parent=used;
        push(heap,&count,nodes,used++);
    }
    return leaves ? heap[0] : -1;
}
static int domain(int mode) {
    return mode==SYM_BYTE ? 256 : mode==SYM_CHAR ? 0x110000 : 65536;
}
static int valid_symbol(uint32_t x, int mode) {
    return x<(uint32_t)domain(mode) &&
        !(mode==SYM_CHAR && x>=0xd800 && x<=0xdfff);
}

int huff_encode(const uint8_t *in, size_t n, tl_sym_t mode,
                uint8_t **out, size_t *out_len) {
    uint32_t *freq=NULL;
    int *map=NULL, *heap=NULL;
    uint8_t *path=NULL, *buf=NULL;
    Node *nodes=NULL;
    size_t start, length, pos, base, total;
    uint64_t bits=0, bit=0;
    int leaves=0, rc=TL_ERR_NOMEM, d;
    if (!out || !out_len) return TL_ERR_DATA;
    *out=NULL; *out_len=0;
    if ((!in && n) || n>TL_MAX_FILE || mode>SYM_S16 || (int)mode<0)
        return TL_ERR_DATA;
    if (region(in,n,mode,&start,&length)!=TL_OK) return TL_ERR_DATA;
    d=domain(mode);
    freq=calloc((size_t)d,sizeof(*freq)); map=malloc((size_t)d*sizeof(*map));
    if (!freq || !map) goto done;
    for (pos=start;pos<start+length;) freq[next_symbol(in,&pos,mode)]++;
    for (int i=0;i<d;i++) if (freq[i]) leaves++;
    nodes=calloc((size_t)(2*leaves+1),sizeof(*nodes));
    heap=malloc((size_t)(leaves+1)*sizeof(*heap));
    path=malloc((size_t)(2*leaves+1));
    if (!nodes || !heap || !path) goto done;
    for (int i=0,j=0;i<d;i++) if (freq[i]) {
        nodes[j].symbol=(uint32_t)i; nodes[j].frequency=freq[i]; map[i]=j++;
    }
    build(nodes,leaves,heap);
    for (int i=0;i<leaves;i++) {
        int depth=0;
        for (int j=i;nodes[j].parent!=-1;j=nodes[j].parent) depth++;
        bits+=(uint64_t)nodes[i].frequency*(depth ? depth : 1);
    }
    base=24+(size_t)leaves*8+n-length;
    if ((bits+7)/8>SIZE_MAX-base) { rc=TL_ERR_DATA; goto done; }
    total=base+(size_t)((bits+7)/8);
    buf=calloc(total ? total : 1,1);
    if (!buf) goto done;
    memcpy(buf,"HFS1",4); write32(buf+4,(uint32_t)mode);
    write32(buf+8,(uint32_t)n); write32(buf+12,(uint32_t)start);
    write32(buf+16,(uint32_t)length); write32(buf+20,(uint32_t)leaves);
    for (int i=0;i<leaves;i++) {
        write32(buf+24+i*8,nodes[i].symbol);
        write32(buf+28+i*8,nodes[i].frequency);
    }
    pos=24+(size_t)leaves*8;
    if (start) memcpy(buf+pos,in,start);
    if (n-start-length) memcpy(buf+pos+start,in+start+length,n-start-length);
    for (pos=start;pos<start+length;) {
        int j=map[next_symbol(in,&pos,mode)], depth=0;
        while (nodes[j].parent!=-1) {
            int parent=nodes[j].parent;
            path[depth++]=(uint8_t)(nodes[parent].right==j); j=parent;
        }
        if (!depth) path[depth++]=0;
        while (depth) {
            buf[base+(size_t)(bit/8)] |= (uint8_t)(path[--depth]<<(7-bit%8));
            bit++;
        }
    }
    *out=buf; *out_len=total; buf=NULL; rc=TL_OK;
done:
    free(freq); free(map); free(heap); free(path); free(nodes); free(buf);
    return rc;
}

static int put_symbol(uint8_t *out, size_t *pos, size_t end,
                      uint32_t x, int mode) {
    uint8_t bytes[4]; int count=1;
    bytes[0]=(uint8_t)x;
    if (mode==SYM_S16) { count=2; bytes[1]=(uint8_t)(x>>8); }
    if (mode==SYM_CHAR && x>=128) {
        count=x<0x800 ? 2 : x<0x10000 ? 3 : 4;
        for (int i=count-1;i>0;i--) { bytes[i]=(uint8_t)(128|(x&63)); x>>=6; }
        bytes[0]=(uint8_t)((count==2 ? 192 : count==3 ? 224 : 240)|x);
    }
    if ((size_t)count>end-*pos) return TL_ERR_DATA;
    memcpy(out+*pos,bytes,(size_t)count); *pos+=(size_t)count;
    return TL_OK;
}

int huff_decode(const uint8_t *in, size_t n, size_t max_out,
                uint8_t **out, size_t *out_len) {
    Node *nodes=NULL;
    int *heap=NULL;
    uint32_t *seen=NULL;
    uint8_t *buf=NULL;
    size_t original, start, length, base, pos, a, b;
    uint64_t symbols=0, bits=0, bit=0;
    int mode, leaves, root, rc=TL_ERR_DATA;
    if (!out || !out_len) return TL_ERR_DATA;
    *out=NULL; *out_len=0;
    if (!in || n<24 || memcmp(in,"HFS1",4) || read32(in+4)>SYM_S16)
        return TL_ERR_DATA;
    mode=(int)read32(in+4); original=read32(in+8); start=read32(in+12);
    length=read32(in+16);
    if (original>max_out || original>TL_MAX_FILE || start>original ||
        length>original-start || read32(in+20)>(uint32_t)domain(mode)) return rc;
    leaves=(int)read32(in+20);
    if ((size_t)leaves>(n-24)/8 || (size_t)leaves>length) return rc;
    base=24+(size_t)leaves*8;
    if (original-length>n-base || (mode!=SYM_S16 && (start || length!=original))) return rc;
    base+=original-length;
    nodes=calloc((size_t)(2*leaves+1),sizeof(*nodes));
    heap=malloc((size_t)(leaves+1)*sizeof(*heap));
    seen=calloc((size_t)domain(mode),sizeof(*seen));
    buf=malloc(original ? original : 1);
    if (!nodes || !heap || !seen || !buf) { rc=TL_ERR_NOMEM; goto done; }
    for (int i=0;i<leaves;i++) {
        uint32_t x=read32(in+24+i*8), f=read32(in+28+i*8);
        if (!valid_symbol(x,mode) || !f || f>length || seen[x] ||
            (i && x<=nodes[i-1].symbol)) goto done;
        seen[x]=f; symbols+=f; nodes[i].symbol=x; nodes[i].frequency=f;
    }
    if (symbols>length || (!leaves && length)) goto done;
    root=build(nodes,leaves,heap);
    for (int i=0;i<leaves;i++) {
        int depth=0;
        for (int j=i;nodes[j].parent!=-1;j=nodes[j].parent) depth++;
        bits+=(uint64_t)nodes[i].frequency*(depth ? depth : 1);
    }
    if ((bits+7)/8!=n-base) goto done;
    if (bits%8 && (in[n-1]&((1u<<(8-bits%8))-1))) goto done;
    pos=24+(size_t)leaves*8;
    if (start) memcpy(buf,in+pos,start);
    if (original-start-length) memcpy(buf+start+length,in+pos+start,original-start-length);
    pos=start;
    for (uint64_t i=0;i<symbols;i++) {
        int j=root;
        do {
            unsigned value;
            if (bit>=bits) goto done;
            value=(in[base+(size_t)(bit/8)]>>(7-bit%8))&1; bit++;
            if (nodes[j].left==-1) { if (value) goto done; break; }
            j=value ? nodes[j].right : nodes[j].left;
        } while (nodes[j].left!=-1);
        if (!seen[nodes[j].symbol] ||
            put_symbol(buf,&pos,start+length,nodes[j].symbol,mode)!=TL_OK) goto done;
        seen[nodes[j].symbol]--;
    }
    if (pos!=start+length || bit!=bits ||
        region(buf,original,mode,&a,&b)!=TL_OK || a!=start || b!=length) goto done;
    *out=buf; *out_len=original; buf=NULL; rc=TL_OK;
done:
    free(nodes); free(heap); free(seen); free(buf);
    return rc;
}
