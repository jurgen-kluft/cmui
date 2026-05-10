package main

import (
	"flag"
	"fmt"
	"image/png"
	"math/bits"
	"os"
	"path/filepath"
	"slices"
	"time"

	fe "github.com/jurgen-kluft/cmui/docs/tools/FrameEncoder"
)

const (
	SELECTOR_P2  = 0
	SELECTOR_P4  = 1
	SELECTOR_RAW = 2
	SELECTOR_P8  = 3
)

func rgb565(r, g, b uint32) uint16 {
	return uint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
}

func rgba8888_to_rgb565(c uint32) uint16 {
	r := uint32((c >> 16) & 0xFF)
	g := uint32((c >> 8) & 0xFF)
	b := uint32(c & 0xFF)
	return rgb565(r, g, b)
}

type histocolor struct {
	color      uint16 // the RGB565 color value
	colorCount int32  // occurrence count of the color in an image
}

// ------------------------------------------------------------------
// Pixel count for each pixel stream
// ------------------------------------------------------------------
func calculatePixelCounts(hist []histocolor) (p2NumPixels, p4NumPixels, p8NumPixels, p16NumPixels int64) {
	p2NumPixels = int64(0)
	p4NumPixels = int64(0)
	p8NumPixels = int64(0)
	p16NumPixels = int64(0)
	for i, hc := range hist {
		if i < 4 {
			p2NumPixels += int64(hc.colorCount)
		} else if i < 20 {
			p4NumPixels += int64(hc.colorCount)
		} else if i < 276 {
			p8NumPixels += int64(hc.colorCount)
		} else {
			p16NumPixels += int64(hc.colorCount)
		}
	}
	return p2NumPixels, p4NumPixels, p8NumPixels, p16NumPixels
}

// ------------------------------------------------------------------
// Correct histogram color index mapping
// ------------------------------------------------------------------
func makeColorIndex(hist []histocolor) []int32 {
	// The histogram is sorted, so color lookup will be incorrect.
	colorMapping := make([]int32, 65536)
	for i := range hist {
		c := hist[i].color
		colorMapping[c] = int32(i)
	}
	return colorMapping
}

func buildHistogram(pixels []uint16, w, h int) (histogram []histocolor) {

	// Note: avoid the use of map for histogram to ensure deterministic palette generation
	// across different runs and platforms. The histogram is implemented as a fixed-size array
	// indexed by RGB565 color values, which guarantees consistent ordering of colors based on
	// their occurrence counts.
	histogram = make([]histocolor, 65536) // RGB565 histogram
	for i := range histogram {
		histogram[i].color = uint16(i)
		histogram[i].colorCount = 0
	}

	for y := range h {
		for x := range w {
			v := pixels[y*w+x]
			histogram[v].colorCount++
		}
	}

	slices.SortFunc(histogram, func(a, b histocolor) int {
		if a.colorCount < b.colorCount {
			return 1
		} else if a.colorCount > b.colorCount {
			return -1
		} else {
			return 0
		}
	})

	return histogram
}

func printImageInfo(histogram []histocolor, w int, h int) {
	// Print image info:
	// - image name
	// - dimensions
	// - total unique colors
	// - pixel count of P0, P1, P2, and raw pixels

	colorCount := 0
	p2Count := int32(0)
	p4Count := int32(0)
	p8Count := int32(0)
	p16Count := int32(0)

	for i, hc := range histogram {
		if hc.colorCount > 0 {
			colorCount++
			if i < 4 {
				p2Count += hc.colorCount
			} else if i < 20 {
				p4Count += hc.colorCount
			} else if i < 276 {
				p8Count += hc.colorCount
			} else {
				p16Count += hc.colorCount
			}
		}
	}

	fmt.Printf("Dimensions: %dx%d\n", w, h)
	fmt.Printf("Raw Size: %d bytes\n", w*h*2) // RGB565 format uses 2 bytes per pixel
	fmt.Printf("Unique color count: %d\n", colorCount)
	fmt.Printf("P2 pixel count: %d\n", p2Count)
	fmt.Printf("P4 pixel count: %d\n", p4Count)
	fmt.Printf("P8 pixel count: %d\n", p8Count)
	fmt.Printf("P16 pixel count: %d\n", p16Count)
}

type lineInfo struct {
	active                bool
	lineIndex             uint16
	spanStream            *fe.BitStreamWriter
	selectorStream        *fe.BitStreamWriter
	p2Stream              *fe.BitStreamWriter
	p4Stream              *fe.BitStreamWriter
	p8Stream              *fe.BitStreamWriter
	p16Stream             []uint16
	spanStreamEncoded     *fe.BitStreamWriter
	selectorStreamEncoded *fe.BitStreamWriter
	p2StreamEncoded       *fe.BitStreamWriter
	p4StreamEncoded       *fe.BitStreamWriter
	p8StreamEncoded       *fe.BitStreamWriter
}

func (l *lineInfo) initialize(y uint16, w int) {
	l.active = true
	l.lineIndex = uint16(y)
	l.spanStream = fe.NewBitStreamWriter(int64(w))
	l.selectorStream = fe.NewBitStreamWriter(int64(w * 2))
	l.p2Stream = fe.NewBitStreamWriter(int64(w * 2))
	l.p4Stream = fe.NewBitStreamWriter(int64(w * 4))
	l.p8Stream = fe.NewBitStreamWriter(int64(w * 8))
	l.p16Stream = make([]uint16, 0, w)
	l.spanStreamEncoded = fe.NewBitStreamWriter(int64(w))
	l.selectorStreamEncoded = fe.NewBitStreamWriter(int64(w * (2 + 5)))
	l.p2StreamEncoded = fe.NewBitStreamWriter(int64(w * (2 + 5)))
	l.p4StreamEncoded = fe.NewBitStreamWriter(int64(w * (4 + 5)))
	l.p8StreamEncoded = fe.NewBitStreamWriter(int64(w * (8 + 5)))
}

func loadImage(path string) (pixels []uint16, w int, h int) {
	f, err := os.Open(path)
	if err != nil {
		panic(err)
	}
	defer f.Close()
	img, err := png.Decode(f)
	if err != nil {
		panic(err)
	}

	w = img.Bounds().Dx()
	h = img.Bounds().Dy()

	pixels = make([]uint16, w*h)
	for y := img.Bounds().Min.Y; y < img.Bounds().Max.Y; y++ {
		for x := img.Bounds().Min.X; x < img.Bounds().Max.X; x++ {
			r, g, b, _ := img.At(x, y).RGBA()
			pixels[(y-img.Bounds().Min.Y)*w+(x-img.Bounds().Min.X)] = rgb565(r>>8, g>>8, b>>8)
		}
	}

	return pixels, w, h
}

func main() {
	var (
		nextPath  = flag.String("main", "", "main PNG image (required)")
		prevPath  = flag.String("prev", "", "prev PNG image (optional)")
		tileSize  = flag.Int("tile-size", 16, "tile size: 8, 16 or 32")
		prevShift = flag.Bool("prev-shift", false, "shift next image down by 1 line as prev")
		prevFill  = flag.Uint("prev-fill", 0x000000, "fill prev image with a color (black as default)")
	)
	flag.Parse()

	if *nextPath == "" {
		fmt.Println("ERROR: -main image required")
		os.Exit(1)
	}
	if *tileSize != 8 && *tileSize != 16 && *tileSize != 32 {
		fmt.Println("ERROR: -tile-size must be one of: 8, 16 or 32")
		os.Exit(1)
	}

	// ------------------------------------------------------------------
	// Load current image and build histogram, palette, and other data
	// ------------------------------------------------------------------
	curPixels, w, h := loadImage(*nextPath)
	pixelCount := int64(w * h)

	// ------------------------------------------------------------------
	// Prepare previous image, palette, histogram and other data
	// ------------------------------------------------------------------
	prevPixels := make([]uint16, w*h) // default to all black if no prev image
	prevImageName := ""
	if *prevPath != "" {
		pw := 0
		ph := 0
		prevPixels, pw, ph = loadImage(*prevPath)
		if pw != w || ph != h {
			fmt.Printf("ERROR: prev image dimensions (%dx%d) do not match next image dimensions (%dx%d)\n", pw, ph, w, h)
			os.Exit(1)
		}
		prevImageName = *prevPath
	} else {
		prevPixels = make([]uint16, pixelCount)
		if *prevShift {
			copy(prevPixels[w:], curPixels[:pixelCount-int64(w)])
			prevImageName = "prev(shifted)"
		} else {
			// fill prevPixels with the specified color
			fillColor := rgba8888_to_rgb565(uint32(*prevFill))
			for i := range prevPixels {
				prevPixels[i] = fillColor
			}
			prevImageName = "prev(filled)"
		}
	}

	startTime := time.Now()

	hist := buildHistogram(curPixels, w, h)
	p2NumPixels, p4NumPixels, p8NumPixels, p16NumPixels := calculatePixelCounts(hist)
	colorMapping := makeColorIndex(hist)

	tileWidth := *tileSize
	tileHeight := *tileSize
	tileWidthShift := bits.TrailingZeros(uint(tileWidth))
	numTilesX := (w + tileWidth - 1) / tileWidth
	numTilesY := (h + tileHeight - 1) / tileHeight
	numTilesTotal := numTilesX * numTilesY

	// ------------------------------------------------------------------
	// Encoding preparation
	// ------------------------------------------------------------------

	// We are going to encode the image line by line.
	// But before we encode, we are first going to build:
	// - tile-change bits (for the ESP32 to identify dirty tiles to upload to the DISPLAY)
	// - line-change bits
	// - run-change bits
	lineChanged := make([]uint8, h)   // indicates whether each line has any changes compared to the prev image
	spanChanged := make([][]uint8, h) // indicates whether each run of tiles in a line has any changes compared to the prev image, only for lines that have changes

	numChangedLines := 0
	numChangedSpans := 0
	for y := 0; y < h; y++ {
		lineHasChanges := uint8(0)
		curLineSpans := make([]uint8, numTilesX)
		spanChanged[y] = curLineSpans
		curLinePixels := curPixels[y*w : y*w+w]
		prevLinePixels := prevPixels[y*w : y*w+w]
		for x := 0; x < w; {
			if curLinePixels[x] != prevLinePixels[x] {
				lineHasChanges = 1
				tx := x >> tileWidthShift
				curLineSpans[tx] = 1
				numChangedSpans += 1
				// skip x up to the end of the current tile
				x = (tx + 1) << tileWidthShift
			} else {
				x++
			}
		}
		lineChanged[y] = lineHasChanges
		numChangedLines += int(lineHasChanges)
	}

	// ------------------------------------------------------------------
	// Build the line, run and tile streams
	// ------------------------------------------------------------------

	lineStream := fe.NewBitStreamWriter(int64(h))
	lineStreamEncoded := fe.NewBitStreamWriter(int64(h))
	for _, lineHasChanged := range lineChanged {
		lineStream.WriteBits(uint32(lineHasChanged), 1)
	}

	// Build the span and tile streams
	spanStream := fe.NewBitStreamWriter(int64(numTilesX * h))
	spanStreamEncoded := fe.NewBitStreamWriter(int64(numTilesX * h))
	tileStream := fe.NewBitStreamWriter(int64(numTilesTotal))
	tileStreamEncoded := fe.NewBitStreamWriter(int64(numTilesTotal))
	tilesChangedTotal := 0
	for ty := 0; ty < numTilesY; ty++ {
		ys := ty * tileHeight
		ye := min((ty+1)*tileHeight, h)
		for tx := 0; tx < numTilesX; tx++ {
			tileHasChanged := uint8(0)
			for y := ys; y < ye; y++ {
				if spanChanged[y][tx] == 1 {
					tileHasChanged = 1
					break
				}
			}
			tileStream.WriteBits(uint32(tileHasChanged), 1)
			tilesChangedTotal += int(tileHasChanged)
		}
	}

	for y := 0; y < h; y++ {
		if lineChanged[y] == 1 {
			lineSpanChanged := spanChanged[y]
			for tx := 0; tx < numTilesX; tx++ {
				spanStream.WriteBits(uint32(lineSpanChanged[tx]), 1)
			}
		}
	}

	// Finalize these streams since we are done writing to them
	lineStreamNumBits, lineStreamNumBytes := lineStream.Finalize()
	spanStreamNumBits, spanStreamNumBytes := spanStream.Finalize()
	tileStreamNumBits, tileStreamNumBytes := tileStream.Finalize()

	// ------------------------------------------------------------------
	// encoding using SRLEN + BitStream
	// Compress the line, run and tile streams
	// ------------------------------------------------------------------
	lineStreamRb, _ := fe.Encode(lineStream.Reader(), 1, nil, lineStreamEncoded)
	spanStreamRb, _ := fe.Encode(spanStream.Reader(), 1, nil, spanStreamEncoded)
	tileStreamRb, _ := fe.Encode(tileStream.Reader(), 1, nil, tileStreamEncoded)

	_, lineStreamEncodedNumBytes := lineStreamEncoded.Finalize()
	_, spanStreamEncodedNumBytes := spanStreamEncoded.Finalize()
	_, tileStreamEncodedNumBytes := tileStreamEncoded.Finalize()

	// ------------------------------------------------------------------
	// Encoding into selector and pixel streams
	// ------------------------------------------------------------------

	lineInfos := make([]lineInfo, h)

	globalSelectorStream := fe.NewBitStreamWriter(int64(pixelCount * 2))
	globalP2Stream := fe.NewBitStreamWriter(int64(p2NumPixels * 2))
	globalP4Stream := fe.NewBitStreamWriter(int64(p4NumPixels * 4))
	globalP8Stream := fe.NewBitStreamWriter(int64(p8NumPixels * 8))

	for ty := 0; ty < h; ty += 1 {
		if lineChanged[ty] == 1 {
			lineInfos[ty].initialize(uint16(ty), w)

			curLinePixels := curPixels[ty*w : ty*w+w]
			curSpanChanged := spanChanged[ty]

			for tx := 0; tx < w; tx += tileWidth {
				spanHasChanged := curSpanChanged[tx>>tileWidthShift]
				spanStream.WriteBits(uint32(spanHasChanged), 1)
				lineInfos[ty].spanStream.WriteBits(uint32(spanHasChanged), 1)

				// The span has changes, so we need to write pixels
				if spanHasChanged == 1 {
					pxe := min(tx+tileWidth, w)
					for pxi := tx; pxi < pxe; pxi += 1 {
						v := curLinePixels[pxi]
						ci := colorMapping[v]
						if ci >= 0 && ci < 4 {
							globalSelectorStream.WriteBits(SELECTOR_P2, 2)
							globalP2Stream.WriteBits(uint32(ci), 2)
							lineInfos[ty].selectorStream.WriteBits(SELECTOR_P2, 2)
							lineInfos[ty].p2Stream.WriteBits(uint32(ci), 2)
						} else if ci >= 4 && ci < 20 {
							globalSelectorStream.WriteBits(SELECTOR_P4, 2)
							globalP4Stream.WriteBits(uint32(ci-4), 4)
							lineInfos[ty].selectorStream.WriteBits(SELECTOR_P4, 2)
							lineInfos[ty].p4Stream.WriteBits(uint32(ci-4), 4)
						} else if ci >= 20 && ci < 276 {
							globalSelectorStream.WriteBits(SELECTOR_P8, 2)
							globalP8Stream.WriteBits(uint32(ci-20), 8)
							lineInfos[ty].selectorStream.WriteBits(SELECTOR_P8, 2)
							lineInfos[ty].p8Stream.WriteBits(uint32(ci-20), 8)
						} else {
							globalSelectorStream.WriteBits(SELECTOR_RAW, 2)
							lineInfos[ty].selectorStream.WriteBits(SELECTOR_RAW, 2)
							lineInfos[ty].p16Stream = append(lineInfos[ty].p16Stream, v)
						}
					}
				}
			}
		}
	}

	globalSelectorStreamNumBits, globalSelectorStreamNumBytes := globalSelectorStream.Finalize()
	globalP2StreamNumBits, globalP2StreamNumBytes := globalP2Stream.Finalize()
	globalP4StreamNumBits, globalP4StreamNumBytes := globalP4Stream.Finalize()
	globalP8StreamNumBits, globalP8StreamNumBytes := globalP8Stream.Finalize()

	// ------------------------------------------------------------------
	// encoding of the global streams using SRLEN + BitStream
	// this is done to get the global rb values that we will use for encoding
	// these streams per line.
	// ------------------------------------------------------------------
	globalSelectorStreamEncoded := fe.NewBitStreamWriter(int64(pixelCount * 2))
	globalP2StreamEncoded := fe.NewBitStreamWriter(int64(p2NumPixels * 2))
	globalP4StreamEncoded := fe.NewBitStreamWriter(int64(p4NumPixels * 4))
	globalP8StreamEncoded := fe.NewBitStreamWriter(int64(p8NumPixels * 8))

	globalSelectorStreamRb, _ := fe.Encode(globalSelectorStream.Reader(), 2, nil, globalSelectorStreamEncoded)
	globalP2StreamRb, _ := fe.Encode(globalP2Stream.Reader(), 2, nil, globalP2StreamEncoded)
	globalP4StreamRb, _ := fe.Encode(globalP4Stream.Reader(), 4, nil, globalP4StreamEncoded)
	globalP8StreamRb, _ := fe.Encode(globalP8Stream.Reader(), 8, nil, globalP8StreamEncoded)

	_, globalSelectorStreamEncodedNumBytes := globalSelectorStreamEncoded.Finalize()
	_, globalP2StreamEncodedNumBytes := globalP2StreamEncoded.Finalize()
	_, globalP4StreamEncodedNumBytes := globalP4StreamEncoded.Finalize()
	_, globalP8StreamEncodedNumBytes := globalP8StreamEncoded.Finalize()

	// ------------------------------------------------------------------
	// Now for each line that has changes, we compress the necessary streams
	// ------------------------------------------------------------------
	lineBasedSpanStreamNumBytes := 0
	lineBasedSelectorStreamNumBytes := 0
	lineBasedP2StreamNumBytes := 0
	lineBasedP4StreamNumBytes := 0
	lineBasedP8StreamNumBytes := 0

	lineBasedSpanStreamEncodedNumBytes := 0
	lineBasedSelectorStreamEncodedNumBytes := 0
	lineBasedP2StreamEncodedNumBytes := 0
	lineBasedP4StreamEncodedNumBytes := 0
	lineBasedP8StreamEncodedNumBytes := 0

	for ty := 0; ty < h; ty += 1 {
		if lineInfos[ty].active {
			_, spanStreamNumBytes := lineInfos[ty].spanStream.Finalize()
			_, selectorStreamNumBytes := lineInfos[ty].selectorStream.Finalize()
			_, p2StreamNumBytes := lineInfos[ty].p2Stream.Finalize()
			_, p4StreamNumBytes := lineInfos[ty].p4Stream.Finalize()
			_, p8StreamNumBytes := lineInfos[ty].p8Stream.Finalize()

			lineBasedSpanStreamNumBytes += int(spanStreamNumBytes)
			lineBasedSelectorStreamNumBytes += int(selectorStreamNumBytes)
			lineBasedP2StreamNumBytes += int(p2StreamNumBytes)
			lineBasedP4StreamNumBytes += int(p4StreamNumBytes)
			lineBasedP8StreamNumBytes += int(p8StreamNumBytes)

			_, err := fe.Encode(lineInfos[ty].spanStream.Reader(), 1, spanStreamRb, lineInfos[ty].spanStreamEncoded)
			if err != nil {
				fmt.Printf("ERROR: failed to encode span stream for line %d: %v\n", ty, err)
				return
			}
			_, err = fe.Encode(lineInfos[ty].selectorStream.Reader(), 2, globalSelectorStreamRb, lineInfos[ty].selectorStreamEncoded)
			if err != nil {
				fmt.Printf("ERROR: failed to encode selector stream for line %d: %v\n", ty, err)
				return
			}
			_, err = fe.Encode(lineInfos[ty].p2Stream.Reader(), 2, globalP2StreamRb, lineInfos[ty].p2StreamEncoded)
			if err != nil {
				fmt.Printf("ERROR: failed to encode P2 stream for line %d: %v\n", ty, err)
				return
			}
			_, err = fe.Encode(lineInfos[ty].p4Stream.Reader(), 4, globalP4StreamRb, lineInfos[ty].p4StreamEncoded)
			if err != nil {
				fmt.Printf("ERROR: failed to encode P4 stream for line %d: %v\n", ty, err)
				return
			}
			_, err = fe.Encode(lineInfos[ty].p8Stream.Reader(), 8, globalP8StreamRb, lineInfos[ty].p8StreamEncoded)
			if err != nil {
				fmt.Printf("ERROR: failed to encode P8 stream for line %d: %v\n", ty, err)
				return
			}

			// Finalize the encoded streams for this line to get the final byte counts for the report
			_, spanStreamEncodedNumBytes := lineInfos[ty].spanStreamEncoded.Finalize()
			_, selectorStreamEncodedNumBytes := lineInfos[ty].selectorStreamEncoded.Finalize()
			_, p2StreamEncodedNumBytes := lineInfos[ty].p2StreamEncoded.Finalize()
			_, p4StreamEncodedNumBytes := lineInfos[ty].p4StreamEncoded.Finalize()
			_, p8StreamEncodedNumBytes := lineInfos[ty].p8StreamEncoded.Finalize()

			lineBasedSpanStreamEncodedNumBytes += int(spanStreamEncodedNumBytes)
			lineBasedSelectorStreamEncodedNumBytes += int(selectorStreamEncodedNumBytes)
			lineBasedP2StreamEncodedNumBytes += int(p2StreamEncodedNumBytes)
			lineBasedP4StreamEncodedNumBytes += int(p4StreamEncodedNumBytes)
			lineBasedP8StreamEncodedNumBytes += int(p8StreamEncodedNumBytes)
		}
	}

	// ------------------------------------------------------------------
	// Setup the frame header
	// ------------------------------------------------------------------
	header := fe.NewFrameHeader()
	header.SetImgDimensions(uint16(w), uint16(h))
	header.SetTileDimensions(uint8(tileWidth), uint8(tileHeight))

	header.SetP8(globalP8StreamRb, uint32(globalP8StreamEncodedNumBytes), uint32(globalP8StreamNumBits/8))
	header.SetP4(globalP4StreamRb, uint32(globalP4StreamEncodedNumBytes), uint32(globalP4StreamNumBits/4))
	header.SetP2(globalP2StreamRb, uint32(globalP2StreamEncodedNumBytes), uint32(globalP2StreamNumBits/2))
	header.SetSelector(globalSelectorStreamRb, uint32(globalSelectorStreamEncodedNumBytes), uint32(globalSelectorStreamNumBits/2))
	header.SetSpanChange(spanStreamRb, uint32(spanStreamEncodedNumBytes), uint32(spanStreamNumBits/1))
	header.SetTileChange(tileStreamRb, uint32(tileStreamEncodedNumBytes), uint32(tileStreamNumBits/1))
	header.SetLineChange(lineStreamRb, uint32(lineStreamEncodedNumBytes), uint32(lineStreamNumBits/1))

	// ------------------------------------------------------------------
	// Using the LineInfo for each active line we can now build the full
	// frame data that can be send over TCP to the ESP32.
	// - frame header
	//   - image width, height
	//   - tile width, tile height
	//   - global selector rb array
	//   - global span rb array
	//   - global P2 rb array
	//   - global P4 rb array
	//   - global P8 rb array
	//   - line change array
	//   - tile change array
	// - per active line:
	//   - length of msg (u16)
	//   - line index (u16)
	//   - P16 stream; length(u16), data (u16*)
	//   - P8 stream; length(u16), data (u8*)
	//   - P4 stream; length(u16), data (u8*)
	//   - P2 stream; length(u16), data (u8*)
	//   - selector; length(u16), data (u8*)
	//   - span change stream; length(u16), data (u8*)
	//   - alignment to 4 bytes (if necessary)
	// - frame end marker

	// ------------------------------------------------------------------

	endTime := time.Now()
	elapsed := endTime.Sub(startTime)

	// Some global stats of the selector and pixel streams
	fmt.Println("----------------------------------------")
	fmt.Printf("Global span stream: %d bytes\n", spanStreamNumBytes)
	fmt.Printf("Global selector stream: %d bytes\n", globalSelectorStreamNumBytes)
	fmt.Printf("Global P2 stream: %d bytes\n", globalP2StreamNumBytes)
	fmt.Printf("Global P4 stream: %d bytes\n", globalP4StreamNumBytes)
	fmt.Printf("Global P8 stream: %d bytes\n", globalP8StreamNumBytes)
	fmt.Println()
	fmt.Printf("Line-based span stream bytes: %d\n", lineBasedSpanStreamNumBytes)
	fmt.Printf("Line-based selector stream bytes: %d\n", lineBasedSelectorStreamNumBytes)
	fmt.Printf("Line-based P2 stream bytes: %d\n", lineBasedP2StreamNumBytes)
	fmt.Printf("Line-based P4 stream bytes: %d\n", lineBasedP4StreamNumBytes)
	fmt.Printf("Line-based P8 stream bytes: %d\n", lineBasedP8StreamNumBytes)
	fmt.Println("---- encoded -----")
	fmt.Printf("Global span stream: %d bytes\n", spanStreamEncodedNumBytes)
	fmt.Printf("Global selector stream: %d bytes\n", globalSelectorStreamEncodedNumBytes)
	fmt.Printf("Global P2 stream: %d bytes\n", globalP2StreamEncodedNumBytes)
	fmt.Printf("Global P4 stream: %d bytes\n", globalP4StreamEncodedNumBytes)
	fmt.Printf("Global P8 stream: %d bytes\n", globalP8StreamEncodedNumBytes)
	fmt.Println()
	fmt.Printf("Line-based span stream bytes: %d\n", lineBasedSpanStreamEncodedNumBytes)
	fmt.Printf("Line-based selector stream bytes: %d\n", lineBasedSelectorStreamEncodedNumBytes)
	fmt.Printf("Line-based P2 stream bytes: %d\n", lineBasedP2StreamEncodedNumBytes)
	fmt.Printf("Line-based P4 stream bytes: %d\n", lineBasedP4StreamEncodedNumBytes)
	fmt.Printf("Line-based P8 stream bytes: %d\n", lineBasedP8StreamEncodedNumBytes)
	fmt.Println("----------------------------------------")

	// ------------------------------------------------------------------
	// Print current and previous image info
	// ------------------------------------------------------------------
	fmt.Println("----------------------------------------")
	fmt.Printf("Image: %s\n", filepath.Base(*nextPath))
	printImageInfo(hist, w, h)
	fmt.Println("----------------------------------------")
	prevHist := buildHistogram(prevPixels, w, h)
	fmt.Printf("Previous Image: %s\n", prevImageName)
	printImageInfo(prevHist, w, h)
	fmt.Println("----------------------------------------")

	// ------------------------------------------------------------------
	// Tile report
	// ------------------------------------------------------------------
	fmt.Println("Diff Information:")
	fmt.Printf("   Tile Size: %dx%d\n", tileWidth, tileHeight)
	fmt.Printf("   Tile Count: %dx%d = %d\n", numTilesX, numTilesY, numTilesTotal)
	fmt.Printf("   Tiles Changed: %d (%.2f%%)\n", tilesChangedTotal, float64(tilesChangedTotal)/float64(numTilesTotal)*100)
	fmt.Printf("   Lines Changed: %d\n", numChangedLines)
	fmt.Printf("   Spans Changed: %d\n", numChangedSpans)
	fmt.Println("----------------------------------------")

	// ------------------------------------------------------------------
	// Report
	// ------------------------------------------------------------------
	rawBytes := pixelCount * 2
	fmt.Printf("Streams:\n")
	fmt.Printf("P16      : %6d bytes\n", p16NumPixels*2)
	fmt.Printf("P8       : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", globalP8StreamNumBytes, globalP8StreamEncodedNumBytes, float64(globalP8StreamNumBytes)/float64(globalP8StreamEncodedNumBytes))
	fmt.Printf("P4       : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", globalP4StreamNumBytes, globalP4StreamEncodedNumBytes, float64(globalP4StreamNumBytes)/float64(globalP4StreamEncodedNumBytes))
	fmt.Printf("P2       : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", globalP2StreamNumBytes, globalP2StreamEncodedNumBytes, float64(globalP2StreamNumBytes)/float64(globalP2StreamEncodedNumBytes))
	fmt.Printf("Selector : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", globalSelectorStreamNumBytes, globalSelectorStreamEncodedNumBytes, float64(globalSelectorStreamNumBytes)/float64(globalSelectorStreamEncodedNumBytes))
	fmt.Printf("Line     : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", lineStreamNumBytes, lineStreamEncodedNumBytes, float64(lineStreamNumBytes)/float64(lineStreamEncodedNumBytes))
	fmt.Printf("Span     : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", spanStreamNumBytes, spanStreamEncodedNumBytes, float64(spanStreamNumBytes)/float64(spanStreamEncodedNumBytes))
	fmt.Printf("Tile     : %6d bytes -> %6d bytes (SRLEN, ratio %.2fx)\n", tileStreamNumBytes, tileStreamEncodedNumBytes, float64(tileStreamNumBytes)/float64(tileStreamEncodedNumBytes))
	fmt.Println("----------------------------------------")

	total := int64(lineStreamEncodedNumBytes)
	total += int64(spanStreamEncodedNumBytes)
	total += int64(tileStreamEncodedNumBytes)
	total += int64(globalSelectorStreamEncodedNumBytes)
	total += int64(globalP2StreamEncodedNumBytes)
	total += int64(globalP4StreamEncodedNumBytes)
	total += int64(globalP8StreamEncodedNumBytes)
	total += int64(p16NumPixels * 2)
	fmt.Printf("Total encoded: %d bytes (%.2fx)\n", total, float64(rawBytes)/float64(total))
	fmt.Printf("Total encoding time: %s\n", elapsed)
}
