package main

import (
	"flag"
	"fmt"
	"image/png"
	"os"
	"path/filepath"
	"slices"

	fe "github.com/jurgen-kluft/cmui/docs/tools/FrameEncoder"
)

const (
	SELECTOR_P0  = 0
	SELECTOR_P1  = 1
	SELECTOR_RAW = 2
	SELECTOR_P2  = 3
)

func rgb565(r, g, b uint32) uint16 {
	return uint16(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
}

type histocolor struct {
	color      uint16 // the RGB565 color value
	colorCount int32  // occurrence count of the color in an image
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

func printImageInfo(path string, histogram []histocolor, w int, h int) {
	// Print image info:
	// - image name
	// - dimensions
	// - total unique colors
	// - pixel count of P0, P1, P2, and raw pixels

	colorCount := 0
	p2Count := int32(0)
	p4Count := int32(0)
	p16Count := int32(0)
	rawCount := int32(0)

	for i, hc := range histogram {
		if hc.colorCount > 0 {
			colorCount++
			if i < 4 {
				p2Count += hc.colorCount
			} else if i < 20 {
				p4Count += hc.colorCount
			} else if i < 276 {
				p16Count += hc.colorCount
			} else {
				rawCount += hc.colorCount
			}
		}
	}

	fmt.Printf("Image: %s\n", filepath.Base(path))
	fmt.Printf("Dimensions: %dx%d\n", w, h)
	fmt.Printf("Unique color count: %d\n", colorCount)
	fmt.Printf("P2 pixel count: %d\n", p2Count)
	fmt.Printf("P4 pixel count: %d\n", p4Count)
	fmt.Printf("P16 pixel count: %d\n", p16Count)
	fmt.Printf("Raw pixel count: %d\n", rawCount)
}

func main() {
	var (
		prevPath  = flag.String("prev", "", "previous PNG image (optional)")
		nextPath  = flag.String("next", "", "next PNG image (required)")
		runSize   = flag.Int("run", 32, "run size: 16, 32, or 64")
		prevShift = flag.Bool("prev-shift", false, "shift next image down by 1 line as prev")
	)
	flag.Parse()

	if *nextPath == "" {
		fmt.Println("ERROR: -next image required")
		os.Exit(1)
	}
	if *runSize != 16 && *runSize != 32 && *runSize != 64 {
		fmt.Println("ERROR: -run must be one of: 16, 32, 64")
		os.Exit(1)
	}

	// ------------------------------------------------------------------
	// Load current image and build histogram, palette, and other data
	// ------------------------------------------------------------------
	curPixels, w, h := loadImage(*nextPath)
	hist := buildHistogram(curPixels, w, h)
	pixelCount := w * h

	// Print current image info before encoding (for debugging and analysis)
	printImageInfo(*nextPath, hist, w, h)

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
			copy(prevPixels[w:], curPixels[:pixelCount-w])
			prevImageName = "prev(shifted)"
		} else {
			copy(prevPixels, curPixels)
			prevImageName = "prev(filled)"
		}
	}
	prevHist := buildHistogram(prevPixels, w, h)

	// Print previous image info before encoding (for debugging and analysis)
	printImageInfo(prevImageName, prevHist, w, h)

	// ------------------------------------------------------------------
	// Build logical symbol streams
	// ------------------------------------------------------------------
	lineStream := fe.NewBitStreamWriter((h*1 + 7) / 8)              // pre-allocate output buffer
	runStream := fe.NewBitStreamWriter((pixelCount*1 + 7) / 8)      // pre-allocate output buffer
	selectorStream := fe.NewBitStreamWriter((pixelCount*2 + 7) / 8) // pre-allocate output buffer

	p2NumPixels := 0
	p4NumPixels := 0
	p8NumPixels := 0
	rawNumPixels := 0
	for i, hc := range hist {
		if i < 4 {
			p2NumPixels += int(hc.colorCount)
		} else if i < 20 {
			p4NumPixels += int(hc.colorCount)
		} else if i < 276 {
			p8NumPixels += int(hc.colorCount)
		} else {
			rawNumPixels += int(hc.colorCount)
		}
	}

	p0Stream := fe.NewBitStreamWriter((p2NumPixels*2 + 7) / 8) // pre-allocate output buffer
	p1Stream := fe.NewBitStreamWriter((p4NumPixels*4 + 7) / 8) // pre-allocate output buffer
	p2Stream := fe.NewBitStreamWriter((p8NumPixels*8 + 7) / 8) // pre-allocate output buffer

	rawPixelCount := pixelCount - p2NumPixels - p4NumPixels - p8NumPixels
	rawStream := make([]uint16, 0, rawPixelCount) // raw pixel values for colors not in the palette

	// The histogram is sorted, so color lookup will be incorrect.
	colorIndex := make([]int32, 65536)
	for i := range hist {
		c := hist[i].color
		colorIndex[c] = int32(i)
	}

	for y := 0; y < h; y++ {
		lineChanged := uint32(0)
		for x := range w {
			if curPixels[y*w+x] != prevPixels[y*w+x] {
				lineChanged = 1
				break
			}
		}
		lineStream.WriteBits(lineChanged, 1)
		if lineChanged == 0 {
			continue
		}

		for x0 := 0; x0 < w; x0 += *runSize {
			rc := uint32(0)
			for i := 0; i < *runSize && x0+i < w; i++ {
				if curPixels[y*w+x0+i] != prevPixels[y*w+x0+i] {
					rc = 1
					break
				}
			}
			runStream.WriteBits(rc, 1)
			if rc == 0 {
				continue
			}

			for i := 0; i < *runSize && x0+i < w; i++ {
				v := curPixels[y*w+x0+i]
				// Determine the selector and corresponding palette index for the current pixel value
				// Does this pixel belong to P0, P1, P2, or should it be encoded as raw?
				ci := colorIndex[v]
				if ci >= 0 && ci < 4 {
					selectorStream.WriteBits(SELECTOR_P0, 2)
					p0Stream.WriteBits(uint32(ci), 2)
				} else if ci >= 4 && ci < 20 {
					selectorStream.WriteBits(SELECTOR_P1, 2)
					p1Stream.WriteBits(uint32(ci-4), 4)
				} else if ci >= 20 && ci < 276 {
					selectorStream.WriteBits(SELECTOR_P2, 2)
					p2Stream.WriteBits(uint32(ci-20), 8)
				} else {
					selectorStream.WriteBits(SELECTOR_RAW, 2)
					rawStream = append(rawStream, v)
				}
			}
		}
	}

	lineStreamNumBits := lineStream.Finalize()
	runStreamNumBits := runStream.Finalize()
	selectorStreamNumBits := selectorStream.Finalize()
	p0StreamNumBits := p0Stream.Finalize()
	p1StreamNumBits := p1Stream.Finalize()
	p2StreamNumBits := p2Stream.Finalize()

	// ------------------------------------------------------------------
	// encoding using SRLEN + BitStream
	// ------------------------------------------------------------------
	lineEncoded := fe.NewBitStreamWriter((lineStreamNumBits + 7) / 8)    // pre-allocate output buffer
	runEncoded := fe.NewBitStreamWriter((runStreamNumBits + 7) / 8)      // pre-allocate output buffer
	selEncoded := fe.NewBitStreamWriter((selectorStreamNumBits + 7) / 8) // pre-allocate output buffer
	p0Encoded := fe.NewBitStreamWriter((p0StreamNumBits + 7) / 8)        // pre-allocate output buffer
	p1Encoded := fe.NewBitStreamWriter((p1StreamNumBits + 7) / 8)        // pre-allocate output buffer
	p2Encoded := fe.NewBitStreamWriter((p2StreamNumBits + 7) / 8)        // pre-allocate output buffer

	fe.Encode(lineStream.Reader(), 1, 2, lineEncoded)
	fe.Encode(runStream.Reader(), 1, 2, runEncoded)
	fe.Encode(selectorStream.Reader(), 2, 4, selEncoded)
	fe.Encode(p0Stream.Reader(), 2, 4, p0Encoded)
	fe.Encode(p1Stream.Reader(), 4, 16, p1Encoded)
	fe.Encode(p2Stream.Reader(), 8, 256, p2Encoded)

	lineEncodedNumBits := lineEncoded.Finalize()
	runEncodedNumBits := runEncoded.Finalize()
	selEncodedNumBits := selEncoded.Finalize()
	p0EncodedNumBits := p0Encoded.Finalize()
	p1EncodedNumBits := p1Encoded.Finalize()
	p2EncodedNumBits := p2Encoded.Finalize()

	// ------------------------------------------------------------------
	// Report
	// ------------------------------------------------------------------
	rawBytes := pixelCount * 2
	fmt.Printf("Image: %dx%d (%d px)\n", w, h, pixelCount)
	fmt.Printf("RAW RGB565: %d bytes\n", rawBytes)
	fmt.Printf("Line     : %6d bytes (SRLEN)\n", (lineEncodedNumBits+7)/8)
	fmt.Printf("Run      : %6d bytes (SRLEN)\n", (runEncodedNumBits+7)/8)
	fmt.Printf("Selector : %6d bytes (SRLEN)\n", (selEncodedNumBits+7)/8)
	fmt.Printf("P0       : %6d bytes (SRLEN)\n", (p0EncodedNumBits+7)/8)
	fmt.Printf("P1       : %6d bytes (SRLEN)\n", (p1EncodedNumBits+7)/8)
	fmt.Printf("P2       : %6d bytes (SRLEN)\n", (p2EncodedNumBits+7)/8)
	fmt.Printf("RAW P3   : %6d bytes\n", len(rawStream)*2)

	total := int((lineEncodedNumBits + 7) / 8)
	total += (runEncodedNumBits + 7) / 8
	total += (selEncodedNumBits + 7) / 8
	total += (p0EncodedNumBits + 7) / 8
	total += (p1EncodedNumBits + 7) / 8
	total += (p2EncodedNumBits + 7) / 8
	total += len(rawStream) * 2

	fmt.Printf("Total encoded: %d bytes (%.2fx)\n", total, float64(rawBytes)/float64(total))
}
