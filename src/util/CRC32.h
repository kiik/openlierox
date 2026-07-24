#ifndef OLX_UTIL_CRC32_H
#define OLX_UTIL_CRC32_H

#include <zlib.h>
#include <stddef.h>
#include <stdint.h>

// Drop-in replacement for boost::crc_32_type built on zlib's crc32().
// zlib uses the same CRC-32 parameters (reflected input/output,
// init and final xor 0xFFFFFFFF), so checksums are bit-identical
// to the former boost implementation -- mod and network CRCs stay compatible.
class CRC32 {
public:
	typedef uint32_t value_type;

	CRC32() : m_crc(crc32(0, Z_NULL, 0)) {}

	void process_byte(uint8_t b) { m_crc = crc32(m_crc, &b, 1); }

	void process_bytes(const void* buffer, size_t byte_count) {
		m_crc = crc32(m_crc, (const Bytef*)buffer, (uInt)byte_count);
	}

	// boost::crc_32_type is also callable as a functor over single bytes.
	void operator()(unsigned char b) { process_byte(b); }

	value_type checksum() const { return (value_type)m_crc; }

private:
	unsigned long m_crc;
};

#endif // OLX_UTIL_CRC32_H
