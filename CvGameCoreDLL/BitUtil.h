#pragma once

#ifndef BIT_UTIL_H
#define BIT_UTIL_H

/*  advc.enum: Helper functions for bitwise operations. Copied from the
	"We the People" mod (all code by Nightinggale), which keeps these functions
	directly in CvGameCoreDLL.h. */

namespace BitUtil // advc: Don't want these in the global namespace
{
	template <typename T>
	bool GetBit(const T x, const int y) // advc: renamed from "HasBit"
	{
		return (x & ((T)1U << y)) != 0;
	}

	template <typename T>
	T SetBit(T &x, const int y)
	{
		return x = (T)(x | ((T)1U << y));
	}

	template <typename T>
	T ClrBit(T &x, const int y)
	{
		return x = (T)(x & ~((T)1U << y));
	}

	// use one of the previous functions instead of hardcoding bValue
	// this function is only for cases where a bit can be turned both on and off
	template <typename T>
	T SetBit(T &x, const int y, const bool bValue)
	{
		// advc: Forward the call instead of duplicating code
		if (bValue)
			return SetBit(x, y);
		return ClrBit(x, y);
	}

// advc: These are unused for now
#if 0
	template <typename T>
	T GetBits(T &x, const int iIndex, const int iNumBits)
	{
		return (x >> iIndex) & (((T)1u << iNumBits) - 1);
	}

	template <typename T>
	T SetBits(T &x, const int iIndex, const int iNumBits, const T iValue)
	{
		x &= ~(((1 << iNumBits) - 1) << iIndex);
		x |= (iValue & ((1 << iNumBits) - 1)) << iIndex;
		return x;
	}
#endif

	/*	<advc> Helpers for representing bits as arrays of uint blocks.
		These were EnumMap members in WtP; moved. */
	inline int getIndexInBlock(int iBitPos)
	{
		// bitmasks to get the bits, which gives the indexes to store 8 or 32 bit.
		// modulo is slow at runtime, binary AND is fast. They give the same result in this case.
		// Maybe the compiler will optimize to a binary and, but explicitly writing it means we are certain it will optimize.
		// In fact this way it's "optimized" even in debug builds.
		return iBitPos & 0x1F;
	}
	
	// Add these for convenience ...
	inline bool getBitFromBlocks(uint const* auiBlocks, int iBitPos)
	{
		return GetBit(
				auiBlocks[iBitPos / 32],
				getIndexInBlock(iBitPos));
	}
	inline void setBitInBlocks(uint* auiBlocks, int iBitPos, bool bValue)
	{
		SetBit(auiBlocks[iBitPos / 32],
				getIndexInBlock(iBitPos),
				bValue);
	}
	// </advc>
};

// advc.enum (not from WtP): Macro for (somewhat) type-safe bitmasks
#define OVERLOAD_BITWISE_OPERATORS(EnumType) \
	inline EnumType operator|(EnumType eLeft, EnumType eRight) \
	{ \
		int iLeft = eLeft, iRight = eRight; \
		return static_cast<EnumType>(iLeft | iRight); \
	} \
	inline EnumType operator|=(EnumType& eLeft, EnumType eRight) \
	{ \
		return eLeft = (eLeft | eRight); \
	} \
	inline EnumType operator&(EnumType eLeft, EnumType eRight) \
	{ \
		int iLeft = eLeft, iRight = eRight; \
		return static_cast<EnumType>(iLeft & iRight); \
	} \
	inline EnumType operator&=(EnumType& eLeft, EnumType eRight) \
	{ \
		return eLeft = (eLeft & eRight); \
	} \
	inline EnumType operator^(EnumType eLeft, EnumType eRight) \
	{ \
		int iLeft = eLeft, iRight = eRight; \
		return static_cast<EnumType>(iLeft ^ iRight); \
	} \
	inline EnumType operator^=(EnumType& eLeft, EnumType eRight) \
	{ \
		return eLeft = (eLeft ^ eRight); \
	} \
	inline EnumType operator~(EnumType e) \
	{ \
		int i = e; \
		return static_cast<EnumType>(~i); \
	}

#endif
