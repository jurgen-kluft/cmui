package remote_ui_server

type ClientInfo struct {
	DeviceType   uint16 // 0=unknown, 1=phone, 2=tablet, 3=desktop
	ScreenWidth  uint16 // in pixels
	ScreenHeight uint16 // in pixels
	MacAddress   [6]byte
}

type UserInterface interface {
	Initialize(spritePack *SpritePack, fontPack *FontPack)

	OnClientInfo(info ClientInfo)

	OnTouch(x, y int)
	OnSwipe(direction int, distance int)
	OnButton(buttonID int, state int)
	OnRotary(rotation int)

	Render() (previous *FrameBuffer, current *FrameBuffer)
}
