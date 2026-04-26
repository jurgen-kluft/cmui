package main

import (
	"flag"
	"fmt"
	"image/png"
	"os"
	"sort"

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

	// ------------------------------------------------------------------
	// Load NEXT image
	// ------------------------------------------------------------------
	f, err := os.Open(*nextPath)
	if err != nil {
		panic(err)
	}
	img, err := png.Decode(f)
	if err != nil {
		panic(err)
	}
	f.Close()

	b := img.Bounds()
	w, h := b.Dx(), b.Dy()
	pixelCount := w * h

	pixels := make([]uint16, 0, pixelCount)
	hist := map[uint16]int{}

	for y := b.Min.Y; y < b.Max.Y; y++ {
		for x := b.Min.X; x < b.Max.X; x++ {
			r, g, b, _ := img.At(x, y).RGBA()
			v := rgb565(r>>8, g>>8, b>>8)
			pixels = append(pixels, v)
			hist[v]++
		}
	}

	// ------------------------------------------------------------------
	// Build palette (RGB565 histogram)
	// ------------------------------------------------------------------
	type kv struct {
		k uint16
		v int
	}
	arr := make([]kv, 0, len(hist))
	for k, v := range hist {
		arr = append(arr, kv{k, v})
	}
	sort.Slice(arr, func(i, j int) bool { return arr[i].v > arr[j].v })

	pal0 := []uint16{}
	pal1 := []uint16{}
	pal2 := []uint16{}
	for i, e := range arr {
		if i < 4 {
			pal0 = append(pal0, e.k)
		} else if i < 20 {
			pal1 = append(pal1, e.k)
		} else if i < 276 {
			pal2 = append(pal2, e.k)
		}
	}

	idx0 := map[uint16]uint32{}
	idx1 := map[uint16]uint32{}
	idx2 := map[uint16]uint32{}
	for i, v := range pal0 {
		idx0[v] = uint32(i)
	}
	for i, v := range pal1 {
		idx1[v] = uint32(i)
	}
	for i, v := range pal2 {
		idx2[v] = uint32(i)
	}

	// ------------------------------------------------------------------
	// Prepare previous buffer
	// ------------------------------------------------------------------
	prev := make([]uint16, pixelCount)
	if *prevShift {
		copy(prev[w:], pixels[:pixelCount-w])
	} else if *prevPath == "" {
		copy(prev, pixels)
	}

	// ------------------------------------------------------------------
	// Build logical symbol streams
	// ------------------------------------------------------------------
	lineStream := fe.NewBitStreamWriter((h*1 + 7) / 8)              // pre-allocate output buffer
	runStream := fe.NewBitStreamWriter((pixelCount*1 + 7) / 8)      // pre-allocate output buffer
	selectorStream := fe.NewBitStreamWriter((pixelCount*2 + 7) / 8) // pre-allocate output buffer

	p0NumPixels := 0
	for _, v := range pal0 {
		p0NumPixels += hist[v]
	}

	p1NumPixels := 0
	for _, v := range pal1 {
		p1NumPixels += hist[v]
	}

	p2NumPixels := 0
	for _, v := range pal2 {
		p2NumPixels += hist[v]
	}

	p0Stream := fe.NewBitStreamWriter((p0NumPixels*2 + 7) / 8) // pre-allocate output buffer
	p1Stream := fe.NewBitStreamWriter((p1NumPixels*4 + 7) / 8) // pre-allocate output buffer
	p2Stream := fe.NewBitStreamWriter((p2NumPixels*8 + 7) / 8) // pre-allocate output buffer
	rawStream := []uint16{}

	for y := 0; y < h; y++ {
		lineChanged := uint32(0)
		for x := 0; x < w; x++ {
			if pixels[y*w+x] != prev[y*w+x] {
				lineChanged = 1
				break
			}
		}
		//line = append(line, lineChanged)
		lineStream.WriteBits(lineChanged, 1)
		if lineChanged == 0 {
			continue
		}

		for x0 := 0; x0 < w; x0 += *runSize {
			rc := uint32(0)
			for i := 0; i < *runSize && x0+i < w; i++ {
				if pixels[y*w+x0+i] != prev[y*w+x0+i] {
					rc = 1
					break
				}
			}
			//run = append(run, rc)
			runStream.WriteBits(rc, 1)
			if rc == 0 {
				continue
			}

			for i := 0; i < *runSize && x0+i < w; i++ {
				v := pixels[y*w+x0+i]
				if id, ok := idx0[v]; ok {
					//selector = append(selector, SELECTOR_P0)
					selectorStream.WriteBits(SELECTOR_P0, 2)
					//p0 = append(p0, id)
					p0Stream.WriteBits(id, 2)
				} else if id, ok := idx1[v]; ok {
					//selector = append(selector, SELECTOR_P1)
					selectorStream.WriteBits(SELECTOR_P1, 2)
					//p1 = append(p1, id)
					p1Stream.WriteBits(id, 4)
				} else if id, ok := idx2[v]; ok {
					//selector = append(selector, SELECTOR_P2)
					selectorStream.WriteBits(SELECTOR_P2, 2)
					//p2 = append(p2, id)
					p2Stream.WriteBits(id, 8)
				} else {
					//selector = append(selector, SELECTOR_RAW)
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
	// ACTUAL encoding using SRLEN + BitStream
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
	// Report (REAL sizes from BitStream)
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
