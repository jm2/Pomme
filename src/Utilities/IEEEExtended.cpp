#include "IEEEExtended.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace
{
static_assert(sizeof(double) == sizeof(std::uint64_t)
		&& std::numeric_limits<double>::is_iec559
		&& std::numeric_limits<double>::digits == 53
		&& std::numeric_limits<double>::max_exponent == 1024,
		"IEEE extended conversion requires an IEEE 754 binary64 double");

constexpr std::uint16_t kExtendedExponentMask = 0x7fff;
constexpr int kExtendedExponentBias = 16383;
constexpr std::uint64_t kExtendedIntegerBit = UINT64_C(1) << 63;
constexpr std::uint64_t kExtendedCanonicalNaN =
		kExtendedIntegerBit | (UINT64_C(1) << 62);

constexpr std::uint64_t kDoubleSignMask = UINT64_C(1) << 63;
constexpr std::uint64_t kDoubleExponentMask = UINT64_C(0x7ff) << 52;
constexpr std::uint64_t kDoubleFractionMask = (UINT64_C(1) << 52) - 1;
constexpr std::uint64_t kDoubleInfinity = kDoubleExponentMask;
constexpr std::uint64_t kDoubleCanonicalNaN =
		kDoubleExponentMask | (UINT64_C(1) << 51);

void StoreBigEndian(std::uint16_t signAndExponent, std::uint64_t significand, char* bytes)
{
	bytes[0] = static_cast<char>(signAndExponent >> 8);
	bytes[1] = static_cast<char>(signAndExponent);
	for (int i = 0; i < 8; i++)
	{
		bytes[2 + i] = static_cast<char>(significand >> (56 - 8 * i));
	}
}

std::uint64_t LoadBigEndianSignificand(const unsigned char* bytes)
{
	std::uint64_t significand = 0;
	for (int i = 0; i < 8; i++)
	{
		significand = (significand << 8) | bytes[2 + i];
	}
	return significand;
}

int HighestSetBit(std::uint64_t value)
{
	return 63 - static_cast<int>(std::countl_zero(value));
}

// Divide by 2^shift, rounding to nearest with ties resolved toward an even
// result. Shifts beyond the source width are handled explicitly so callers can
// use this for binary80 values far below the binary64 range.
std::uint64_t RoundedRightShift(std::uint64_t value, int shift)
{
	if (shift <= 0)
	{
		return value;
	}
	if (shift > 64)
	{
		return 0;
	}
	if (shift == 64)
	{
		const std::uint64_t halfway = UINT64_C(1) << 63;
		return value > halfway ? 1 : 0;
	}

	const std::uint64_t quotient = value >> shift;
	const std::uint64_t remainderMask = (UINT64_C(1) << shift) - 1;
	const std::uint64_t remainder = value & remainderMask;
	const std::uint64_t halfway = UINT64_C(1) << (shift - 1);
	const bool roundUp = remainder > halfway
			|| (remainder == halfway && (quotient & 1) != 0);
	return quotient + static_cast<std::uint64_t>(roundUp);
}

std::uint64_t ScaleToInteger(std::uint64_t value, int binaryScale)
{
	if (binaryScale >= 0)
	{
		return value << binaryScale;
	}
	return RoundedRightShift(value, -binaryScale);
}
}  // namespace

void ConvertToIeeeExtended(double num, char* bytes)
{
	const std::uint64_t bits = std::bit_cast<std::uint64_t>(num);
	const std::uint16_t sign = (bits & kDoubleSignMask) != 0 ? 0x8000 : 0;
	const std::uint16_t exponent =
			static_cast<std::uint16_t>((bits & kDoubleExponentMask) >> 52);
	const std::uint64_t fraction = bits & kDoubleFractionMask;

	std::uint16_t extendedExponent = 0;
	std::uint64_t extendedSignificand = 0;

	if (exponent == 0x7ff)
	{
		extendedExponent = kExtendedExponentMask;
		extendedSignificand = fraction == 0
				? kExtendedIntegerBit
				: kExtendedCanonicalNaN;
	}
	else if (exponent != 0)
	{
		extendedExponent = static_cast<std::uint16_t>(
				exponent - 1023 + kExtendedExponentBias);
		extendedSignificand = ((UINT64_C(1) << 52) | fraction) << 11;
	}
	else if (fraction != 0)
	{
		// A binary64 subnormal is still a normal binary80 value. Normalize the
		// integer significand and adjust the exponent to match its leading bit.
		const int leadingBit = HighestSetBit(fraction);
		const int unbiasedExponent = leadingBit - 1074;
		extendedExponent = static_cast<std::uint16_t>(
				unbiasedExponent + kExtendedExponentBias);
		extendedSignificand = fraction << (63 - leadingBit);
	}

	StoreBigEndian(static_cast<std::uint16_t>(sign | extendedExponent),
			extendedSignificand, bytes);
}

double ConvertFromIeeeExtended(const unsigned char* bytes)
{
	const std::uint16_t signAndExponent =
			static_cast<std::uint16_t>((bytes[0] << 8) | bytes[1]);
	const bool negative = (signAndExponent & 0x8000) != 0;
	const std::uint16_t exponent = signAndExponent & kExtendedExponentMask;
	const std::uint64_t significand = LoadBigEndianSignificand(bytes);
	const std::uint64_t doubleSign = negative ? kDoubleSignMask : 0;

	if (exponent == kExtendedExponentMask)
	{
		const std::uint64_t magnitude = significand == kExtendedIntegerBit
				? kDoubleInfinity
				: kDoubleCanonicalNaN;
		return std::bit_cast<double>(doubleSign | magnitude);
	}
	if (significand == 0)
	{
		return std::bit_cast<double>(doubleSign);
	}

	// The explicit binary80 significand represents
	//     significand * 2^(unbiasedExponent - 63).
	// Normalizing from that expression also gives deterministic behavior for
	// non-canonical encodings whose explicit integer bit is clear.
	const int unbiasedExponent = exponent == 0
			? 1 - kExtendedExponentBias
			: static_cast<int>(exponent) - kExtendedExponentBias;
	const int leadingBit = HighestSetBit(significand);
	int resultExponent = unbiasedExponent - 63 + leadingBit;

	if (resultExponent > 1023)
	{
		return std::bit_cast<double>(doubleSign | kDoubleInfinity);
	}

	if (resultExponent >= -1022)
	{
		const int shift = leadingBit - 52;
		std::uint64_t roundedSignificand = shift >= 0
				? RoundedRightShift(significand, shift)
				: significand << -shift;

		if (roundedSignificand == (UINT64_C(1) << 53))
		{
			roundedSignificand >>= 1;
			resultExponent++;
		}
		if (resultExponent > 1023)
		{
			return std::bit_cast<double>(doubleSign | kDoubleInfinity);
		}

		const std::uint64_t encodedExponent =
				static_cast<std::uint64_t>(resultExponent + 1023) << 52;
		const std::uint64_t encodedFraction =
				roundedSignificand & kDoubleFractionMask;
		return std::bit_cast<double>(doubleSign | encodedExponent | encodedFraction);
	}

	// Binary64 subnormals are integer multiples of 2^-1074. Scale the
	// binary80 significand into those units and round once at the boundary.
	const int scaleInSubnormalUnits = unbiasedExponent + 1011;
	const std::uint64_t subnormal =
			ScaleToInteger(significand, scaleInSubnormalUnits);
	if (subnormal >= (UINT64_C(1) << 52))
	{
		return std::bit_cast<double>(doubleSign | (UINT64_C(1) << 52));
	}
	return std::bit_cast<double>(doubleSign | subnormal);
}
