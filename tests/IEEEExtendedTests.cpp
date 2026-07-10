#include "Utilities/IEEEExtended.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << __FILE__ << ':' << __LINE__ << ": check failed: "           \
                << #condition << '\n';                                          \
      return 1;                                                                 \
    }                                                                           \
  } while (false)

namespace
{
using Extended = std::array<unsigned char, 10>;

bool EncodesAs(double value, const Extended& expected)
{
	std::array<char, 10> encoded{};
	ConvertToIeeeExtended(value, encoded.data());
	for (std::size_t i = 0; i < encoded.size(); i++)
	{
		if (static_cast<unsigned char>(encoded[i]) != expected[i])
		{
			return false;
		}
	}
	return true;
}

bool HasSameBits(double left, double right)
{
	return std::bit_cast<std::uint64_t>(left)
			== std::bit_cast<std::uint64_t>(right);
}
}  // namespace

int main()
{
	const Extended positiveZero{0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended negativeZero{0x80, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended positiveOne{0x3f, 0xff, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended negativeOne{0xbf, 0xff, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended sampleRate{0x40, 0x0e, 0xac, 0x44, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended positiveInfinity{0x7f, 0xff, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended negativeInfinity{0xff, 0xff, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	const Extended canonicalNaN{0x7f, 0xff, 0xc0, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};

	CHECK(EncodesAs(0.0, positiveZero));
	CHECK(EncodesAs(-0.0, negativeZero));
	CHECK(EncodesAs(1.0, positiveOne));
	CHECK(EncodesAs(-1.0, negativeOne));
	CHECK(EncodesAs(44100.0, sampleRate));
	CHECK(EncodesAs(std::numeric_limits<double>::infinity(), positiveInfinity));
	CHECK(EncodesAs(-std::numeric_limits<double>::infinity(), negativeInfinity));
	CHECK(EncodesAs(std::numeric_limits<double>::quiet_NaN(), canonicalNaN));

	CHECK(HasSameBits(ConvertFromIeeeExtended(positiveZero.data()), 0.0));
	CHECK(HasSameBits(ConvertFromIeeeExtended(negativeZero.data()), -0.0));
	CHECK(ConvertFromIeeeExtended(positiveOne.data()) == 1.0);
	CHECK(ConvertFromIeeeExtended(negativeOne.data()) == -1.0);
	CHECK(ConvertFromIeeeExtended(sampleRate.data()) == 44100.0);
	CHECK(std::isinf(ConvertFromIeeeExtended(positiveInfinity.data())));
	CHECK(ConvertFromIeeeExtended(positiveInfinity.data()) > 0.0);
	CHECK(std::isinf(ConvertFromIeeeExtended(negativeInfinity.data())));
	CHECK(ConvertFromIeeeExtended(negativeInfinity.data()) < 0.0);
	CHECK(std::isnan(ConvertFromIeeeExtended(canonicalNaN.data())));

	const std::array<double, 12> roundTripValues{
			-0.0,
			std::numeric_limits<double>::denorm_min(),
			-std::numeric_limits<double>::denorm_min(),
			std::numeric_limits<double>::min(),
			-std::numeric_limits<double>::min(),
			0.1,
			-123456789.25,
			44100.0,
			std::numeric_limits<double>::max(),
			-std::numeric_limits<double>::max(),
			std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(),
	};
	for (double value : roundTripValues)
	{
		std::array<char, 10> encoded{};
		ConvertToIeeeExtended(value, encoded.data());
		const double decoded = ConvertFromIeeeExtended(
				reinterpret_cast<const unsigned char*>(encoded.data()));
		CHECK(HasSameBits(decoded, value));
	}

	// Smallest binary80 subnormal underflows to binary64 zero. A normal
	// binary80 encoding of the smallest binary64 subnormal remains exact.
	const Extended smallestExtendedSubnormal{0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x01};
	const Extended smallestDoubleSubnormal{0x3b, 0xcd, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00};
	CHECK(HasSameBits(ConvertFromIeeeExtended(smallestExtendedSubnormal.data()),
			0.0));
	CHECK(HasSameBits(ConvertFromIeeeExtended(smallestDoubleSubnormal.data()),
			std::numeric_limits<double>::denorm_min()));

	// 1 + 2^-53 is exactly halfway between adjacent binary64 values and must
	// round to the one whose trailing significand bit is even.
	const Extended halfwayAboveOne{0x3f, 0xff, 0x80, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x04, 0x00};
	CHECK(ConvertFromIeeeExtended(halfwayAboveOne.data()) == 1.0);

	return 0;
}
