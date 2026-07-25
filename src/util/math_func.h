#ifndef VERMES_MATH_H
#define VERMES_MATH_H

#include <cmath>
#include <random>
#include "CodeAttributes.h"

/**
 * Magic number goodness!
 */

extern std::mt19937 rndgen;

// Callable wrappers replacing the former boost::uniform_01 / variate_generator
// globals. Both draw from the shared rndgen; the randomness here is cosmetic
// (particle, sound and effect variation), not network-deterministic.
struct Rnd01 {
	double operator()() { return std::uniform_real_distribution<double>(0.0, 1.0)(rndgen); }
};
extern Rnd01 rnd;

struct RndMid {
	double operator()() { return std::uniform_real_distribution<double>(-0.5, 0.5)(rndgen); }
};
extern RndMid midrnd;

float const Pi = 3.14159265358979323846f;

INLINE float deg2rad( float Degrees )
{
	return (Degrees * Pi) / 180;
}

INLINE float rad2deg( float Radians )
{
	return (Radians * 180) / Pi;
}

INLINE unsigned long rndInt(unsigned long max)
{
	// mt19937 emits 32-bit values, so this scales rndgen() into [0, max).
	return static_cast<unsigned long>(
		(static_cast<unsigned long long>(rndgen()) * max) >> 32
	);
}

template<unsigned int B, unsigned int E>
//struct ctpow { static unsigned int const value = ctpow<B*B, E>>1>::value * ((E & 1) ? B : 1); };
struct ctpow { static unsigned int const value = ctpow<B*B, E>::value * ((E & 1) ? B : 1); };

template<unsigned int B>
struct ctpow<B, 0> { static unsigned int const value = 1; };

// MSVC lrintf implementation, taken from http://www.dpvreony.co.uk/blog/post/63
#ifdef _MSC_VER
INLINE long lrintf(float f){

#ifdef _M_X64
	//x64 fix
	//http://groups.google.com/group/openjpeg/browse_thread/thread/64b89cdb1a44bb3b
	return (long)((f > 0.0f) ? (f + 0.5f) : (f - 0.5f)); 
#else 

	int i;
	_asm{
		fld f
		fistp i
	};
	return i;

#endif

}
#endif

INLINE long roundAny(float v)
{
	return lrintf(v);
}

#endif //VERMES_MATH_H
