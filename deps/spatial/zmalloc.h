/* For now just use stdlib malloc, but may be expaned in the future.  */
 
#ifndef ZMALLOC_H_
#define ZMALLOC_H_

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdlib.h>

#define zmalloc(size) (malloc((size)))
#define zcalloc(size) (calloc((size)))
#define zrealloc(ptr,size) (realloc((ptr),(size)))
#define zfree(ptr) (free((ptr)))

#if defined(__cplusplus)
}
#endif
#endif /* ZMALLOC_H_ */