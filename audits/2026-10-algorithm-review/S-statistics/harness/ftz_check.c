#include <stdio.h>
#include <xmmintrin.h>
#include <emmintrin.h>
int main(void) {
	_mm_setcsr(0x9FC0); /* RandomX default: FTZ, DAZ, all exceptions masked, RNE */
	volatile double a = 1e-300, b = 1e-15;
	__m128d r = _mm_mul_pd(_mm_set1_pd(a), _mm_set1_pd(b)); /* 1e-315: subnormal -> flushed */
	unsigned s = _mm_getcsr();
	double out[2]; _mm_storeu_pd(out, r);
	printf("result %g, MXCSR flags 0x%02x (UE=%d PE=%d)\n", out[0], s & 0x3f, (s >> 4) & 1, (s >> 5) & 1);
	return 0;
}
