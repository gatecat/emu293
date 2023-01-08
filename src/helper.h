#pragma once
#include <cstdint>
using namespace std;

//Useful macros
#define check_bit(var, pos)	(!!((var) & (1U << (pos))))
#define toggle_bit(var, pos)	((var) ^= (1U << (pos)))
#define set_bit(var, pos)	((var) |= (1U << (pos)))
#define clear_bit(var, pos)	((var) &= ~(1U << (pos)))
#define get_bits(var, start, count) ((var >> start) & ((1U << (count)) - 1))

#if     defined(__BYTE_ORDER) && __BYTE_ORDER == __LITTLE_ENDIAN \
		|| defined(__LITTLE_ENDIAN__) \
		|| defined(__i386__) || defined(__alpha__) \
		|| defined(__ia64) || defined(__ia64__) \
		|| defined(_M_IX86) || defined(_M_IA64) \
		|| defined(_M_ALPHA) || defined(__amd64) \
		|| defined(__amd64__) || defined(_M_AMD64) \
		|| defined(__x86_64) || defined(__x86_64__) \
		|| defined(_M_X64) || defined(__bfin__)
#define LITTLE_ENDIAN

#else
	#error "Big endian architectures not yet supported (sorry)"
#endif



//General helper functions
namespace Emu293 {
	//convert bytes from array to/from uint32/uint16, little endian
	inline uint32_t get_uint32le(const volatile uint8_t *arr) {
	#ifdef LITTLE_ENDIAN
		return *reinterpret_cast<const volatile uint32_t*>(arr);
	#else
		return 0;
	#endif
	}
	inline uint16_t get_uint16le(const volatile uint8_t *arr) {
	#ifdef LITTLE_ENDIAN
		return *reinterpret_cast<const volatile uint16_t*>(arr);
	#else
		return 0;
	#endif
	}
	inline void set_uint32le(volatile uint8_t *arr, uint32_t val) {
	#ifdef LITTLE_ENDIAN
		*reinterpret_cast<volatile uint32_t*>(arr)=val;
	#else

	#endif
	}
	inline void set_uint16le(volatile uint8_t *arr, uint16_t val) {
	#ifdef LITTLE_ENDIAN
		*reinterpret_cast<volatile uint16_t*>(arr)=val;
	#else

	#endif
	}
	inline uint8_t get_bytele(uint32_t val, uint8_t byte) {
		return (val >> (byte * 8)) & 0xFF;
	}

	inline uint32_t swap_endian(uint32_t val) {
		uint32_t result = 0;
		result |= (val >> 24) & 0xFF;
		result |= (val >> 8) & 0x00FF;
		result |= (val << 8) & 0x0000FF;
		result |= (val << 24) & 0x000000FF;
		return result;
	}
}
