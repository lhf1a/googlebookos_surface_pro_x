/* GNU-EFI provides memcpy/memset but not the transport's ISO memcmp symbol. */
#include <stddef.h>
size_t strlen(const char *s) { size_t n=0;while(s[n])n++;return n; }
int memcmp(const void *a,const void *b,size_t n) {
 const unsigned char *x=a,*y=b;
 for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;
 return 0;
}
