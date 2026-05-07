package mui

type UIInstance interface {
	OnTouch(x, y int)
	OnSwipe(direction int, distance int)
	OnButton(buttonID int, state int)
	OnRotary(rotation int)

	Render() (previous *FrameBuffer, current *FrameBuffer)
}
