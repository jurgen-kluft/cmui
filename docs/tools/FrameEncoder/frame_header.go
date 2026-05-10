package frameencoder

import (
	"bytes"
	"encoding/binary"
)

// C++ structure of the frame header

// struct header_t
// {
//     u16 m_magic;       // 'FE' in ASCII (0x4645), used to identify the encoded frame data
//     u16 m_width;       // Width of the image
//     u16 m_height;      // Height of the image
//     u16 m_run_length;  // Run length for run change stream

//     u8  m_p8_rb[256];         // SRLE run-bits for P8 stream
//     u8  m_p4_rb[16];          // SRLE run-bits for P4 stream
//     u8  m_p2_rb[4];           // SRLE run-bits for P2 stream
//     u8  m_selector_rb[4];     // SRLE run-bits for each symbol
//     u8  m_line_change_rb[2];  // SRLE run-bits for line change stream
//     u8  m_run_change_rb[2];   // SRLE run-bits for run change stream
//     u16 m_palette[276];       // RGB565 palette (4 colors for P2, 16 colors for P4, 256 colors for P8)

//     u32 m_p16_encoded_size;          // size of the encoded P16 stream in bytes
//     u32 m_p8_encoded_size;           // size of the encoded P8 stream in bytes
//     u32 m_p4_encoded_size;           // size of the encoded P4 stream in
//     u32 m_p2_encoded_size;           // size of the encoded P2 stream in bytes
//     u32 m_selector_encoded_size;     // size of the encoded selector stream in bytes
//     u32 m_run_change_encoded_size;   // size of the encoded run change stream in bytes
//     u32 m_tile_change_encoded_size;  // size of the encoded tile change stream in bytes
//     u32 m_line_change_encoded_size;  // size of the encoded line change stream in bytes

//     u32 m_p16_stream_decoded_units;          // size of the decoded P16 stream in units (unit = 16 bits)
//     u32 m_p8_stream_decoded_units;           // size of the decoded P8 stream in units (unit = 8 bits)
//     u32 m_p4_stream_decoded_units;           // size of the decoded P4 stream in units (unit = 4 bits)
//     u32 m_p2_stream_decoded_units;           // size of the decoded P2 stream in units (unit = 2 bits)
//     u32 m_selector_stream_decoded_units;     // size of the decoded selector stream in units (unit = 2 bits)
//     u32 m_run_change_stream_decoded_units;   // size of the decoded run change stream in units (unit = 1 bit)
//     u32 m_tile_change_stream_decoded_units;  // size of the decoded tile change stream in units (unit = 1 bit)
//     u32 m_line_change_stream_decoded_units;  // size of the decoded line change stream in units (unit = 2 bits)
// };

// Golang structure of the frame header

type FrameHeader struct {
	Magic                        uint16
	ImgWidth                     uint16
	ImgHeight                    uint16
	TileWidth                    uint8
	TileHeight                   uint8
	P8RunBits                    []uint8 // 256 elements
	P4RunBits                    []uint8 // 16 elements
	P2RunBits                    []uint8 // 4 elements
	SelectorRunBits              []uint8 // 4 elements
	TileChangeRunBits            []uint8 // 2 elements
	LineChangeRunBits            []uint8 // 2 elements
	Reserved0                    uint16
	Palette                      []uint16 // 276 elements (4 colors for P2, 16 colors for P4, 256 colors for P8)
	TileChangeEncodedSize        uint32
	LineChangeEncodedSize        uint32
	TileChangeStreamDecodedUnits uint32
	LineChangeStreamDecodedUnits uint32
}

func NewFrameHeader() *FrameHeader {
	return &FrameHeader{
		Magic: 0x4645, // 'FE' in ASCII
	}
}

func (fh *FrameHeader) SetImgDimensions(width, height uint16) {
	fh.ImgWidth = width
	fh.ImgHeight = height
}

func (fh *FrameHeader) SetTileDimensions(tileWidth, tileHeight uint8) {
	fh.TileWidth = tileWidth
	fh.TileHeight = tileHeight
}

func (fh *FrameHeader) SetTileChange(runBits []uint8, encodedSize uint32, decodedUnits uint32) {
	fh.TileChangeRunBits = runBits
	fh.TileChangeEncodedSize = encodedSize
	fh.TileChangeStreamDecodedUnits = decodedUnits
}

func (fh *FrameHeader) SetLineChange(runBits []uint8, encodedSize uint32, decodedUnits uint32) {
	fh.LineChangeRunBits = runBits
	fh.LineChangeEncodedSize = encodedSize
	fh.LineChangeStreamDecodedUnits = decodedUnits
}

func (fh *FrameHeader) SetPalette(palette []uint16) {
	fh.Palette = palette
}

func (fh *FrameHeader) WriteBinary(dst bytes.Buffer) {

	// Write the frame header to the provided buffer in binary format
	binary.Write(&dst, binary.LittleEndian, fh.Magic)
	binary.Write(&dst, binary.LittleEndian, fh.ImgWidth)
	binary.Write(&dst, binary.LittleEndian, fh.ImgHeight)
	binary.Write(&dst, binary.LittleEndian, fh.TileWidth)
	binary.Write(&dst, binary.LittleEndian, fh.TileHeight)

	binary.Write(&dst, binary.LittleEndian, fh.P8RunBits)
	binary.Write(&dst, binary.LittleEndian, fh.P4RunBits)
	binary.Write(&dst, binary.LittleEndian, fh.P2RunBits)
	binary.Write(&dst, binary.LittleEndian, fh.SelectorRunBits)
	binary.Write(&dst, binary.LittleEndian, fh.TileChangeRunBits)
	binary.Write(&dst, binary.LittleEndian, fh.LineChangeRunBits)
	binary.Write(&dst, binary.LittleEndian, fh.Reserved0)
	binary.Write(&dst, binary.LittleEndian, fh.Palette)

	binary.Write(&dst, binary.LittleEndian, fh.TileChangeEncodedSize)
	binary.Write(&dst, binary.LittleEndian, fh.LineChangeEncodedSize)
	binary.Write(&dst, binary.LittleEndian, fh.TileChangeStreamDecodedUnits)
	binary.Write(&dst, binary.LittleEndian, fh.LineChangeStreamDecodedUnits)
}
