package srlen

import "slices"

type BitStreamWriter struct {
	buf          []uint8 // buffer for bits being written
	numBits      uint32  // total number of bits written
	pos          uint32  // write byte position
	finalized    bool    // whether Finalize has been called
	accuNumBits  int     // number of bits currently in the accumulator
	accuRegister uint64  // accumulator for bits being written
}

func NewBitStreamWriter(sizeInBits int) *BitStreamWriter {
	bs := &BitStreamWriter{}
	bs.SetCapacity(uint32(sizeInBits))
	return bs
}

func (bs *BitStreamWriter) SetCapacity(sizeInBits uint32) {
	if len(bs.buf) == 0 {
		bs.buf = make([]uint8, (sizeInBits+7)>>3)
	} else {
		needed := (sizeInBits + 7) >> 3
		bs.buf = slices.Grow(bs.buf, int(needed))
		// After grow, length of bs.buf may be less than needed, so we need to reslice to the new length.
		bs.buf = bs.buf[:needed]
	}
}

func (bs *BitStreamWriter) WriteBits(v uint32, n uint8) {
	if n == 0 || bs.finalized {
		return
	}
	// We are accumulating bits in the accuRegister until we have more than 32 bits,
	// at which point we flush to the buffer.
	// Note: This means that we cannot write more than 32 bits at a time!
	bs.accuRegister |= uint64(v) << bs.accuNumBits
	bs.accuNumBits += int(n)
	if bs.accuNumBits >= 32 {
		bs.buf[bs.pos] = uint8(bs.accuRegister & 0xFF)
		bs.buf[bs.pos+1] = uint8((bs.accuRegister >> 8) & 0xFF)
		bs.buf[bs.pos+2] = uint8((bs.accuRegister >> 16) & 0xFF)
		bs.buf[bs.pos+3] = uint8((bs.accuRegister >> 24) & 0xFF)
		bs.pos += 4
		bs.accuRegister >>= 32
		bs.accuNumBits -= 32
	}
	bs.numBits += uint32(n)
}

func (bs *BitStreamWriter) Finalize() (bitsWritten int) {

	// Flush remaining bits in the accumulator to the buffer
	for bs.accuNumBits > 0 {
		bs.buf[bs.pos] = uint8(bs.accuRegister & 0xFF)
		bs.accuRegister >>= 8
		bs.accuNumBits -= 8
		bs.pos++
	}

	bs.accuNumBits = 0
	bs.accuRegister = 0
	bs.finalized = true

	return int(bs.numBits)
}

func (bs *BitStreamWriter) Reader() *BitStreamReader {
	return NewBitStreamReader(bs.buf, bs.numBits)
}

// -------------------------------------------------------------
// BitStreamReader
// ------------------------------------------------------------

type BitStreamReader struct {
	buf          []uint8
	numBits      uint32
	readBits     uint32
	pos          uint32
	accuNumBits  int
	accuRegister uint64
}

func NewBitStreamReader(buf []uint8, numBits uint32) *BitStreamReader {
	return &BitStreamReader{
		buf:     buf,
		numBits: numBits,
	}
}

func (bs *BitStreamReader) ResetRead() {
	bs.readBits = 0
	bs.pos = 0
	bs.accuNumBits = 0
	bs.accuRegister = 0
}

func (bs *BitStreamReader) ReadBits(n uint8) int32 {
	if n == 0 || (bs.readBits+uint32(n)) > bs.numBits {
		return -1
	}

	// Ensure we have more than 32 bits in the accumulator to read from, if not,
	// read more bytes from the buffer.
	for bs.accuNumBits < 32 && bs.pos < uint32(len(bs.buf)) {
		bs.accuRegister |= uint64(bs.buf[bs.pos]) << bs.accuNumBits
		bs.accuNumBits += 8
		bs.pos++
	}

	v := uint32(bs.accuRegister & ((1 << n) - 1))
	bs.accuRegister >>= n
	bs.accuNumBits -= int(n)

	bs.readBits += uint32(n)
	return int32(v)
}

func (bs *BitStreamReader) PeekBits(n uint8) int32 {
	if n == 0 || (bs.readBits+uint32(n)) > bs.numBits {
		return -1
	}

	// Ensure we have more than 32 bits in the accumulator to read from, if not,
	// read more bytes from the buffer.
	for bs.accuNumBits < 32 && bs.pos < uint32(len(bs.buf)) {
		bs.accuRegister |= uint64(bs.buf[bs.pos]) << bs.accuNumBits
		bs.accuNumBits += 8
		bs.pos++
	}

	return int32(bs.accuRegister & ((1 << n) - 1))
}

func (bs *BitStreamReader) SkipBits(n uint8) {
	if n == 0 || (bs.readBits+uint32(n)) > bs.numBits {
		return
	}

	// Ensure we have more than 32 bits in the accumulator to skip from, if not,
	// read more bytes from the buffer.
	for bs.accuNumBits < 32 && bs.pos < uint32(len(bs.buf)) {
		bs.accuRegister |= uint64(bs.buf[bs.pos]) << bs.accuNumBits
		bs.accuNumBits += 8
		bs.pos++
	}

	bs.accuRegister >>= n
	bs.accuNumBits -= int(n)
	bs.readBits += uint32(n)
}

func (bs *BitStreamReader) IsReadEnd(sizeofSymbolInBits uint8) bool {
	return bs.readBits >= bs.numBits || (bs.numBits-bs.readBits) < uint32(sizeofSymbolInBits)
}
