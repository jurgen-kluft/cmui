package mui

import (
	"encoding/binary"
	"net"
)

// A UI instance running on their own go-routine serving a remote client.
// Main driver is the TCP connection, receiving messages from the client and acting on it.

// The connection will wait until the client sends a message, which can be either:
// - Message requesting a UI frame update
//   - render the current page
//   - frame encoder (previous frame and current frame)
//   - send the compressed frame to the client
//   - go back to waiting for the next message
// - Message reporting an input event
//   - call one of the UI instance input functions (OnTouch, OnSwipe, OnButton, OnRotary)

type ConnectionInstance struct {

	// TCP connection to the client
	tcpConn net.Conn

	// The UI instance associated with this connection
	uiInstance UIInstance

	// Frame Encoder

}

// Message Types
type MessageType int

const (
	MessageTypeFrameUpdate MessageType = 1
	MessageTypeInputEvent  MessageType = 2
)

// HandleConnection runs in a go-routine and manages
// the lifecycle of the connection, including receiving
// messages from the client, processing them, and sending
// responses back.
// It will loop indefinitely until the connection is closed
// by either the client or the server.
func (ci *ConnectionInstance) HandleConnection() {

	// - Message size is always < 1280 bytes
	// - We want to re-use the same buffer for each message to avoid unnecessary allocations
	// - When we receive a request to render a frame, we know the client is in a state to
	//   receive the frame data, and so it will not send other messages until it receives
	//   the frame data, so we can safely use the same buffer for both reading messages and
	//   sending frame data.

	buffer := make([]byte, 1280)
	for {
		// Read a message from the client
		n, err := ci.tcpConn.Read(buffer)
		if err != nil {
			// Handle error (e.g., log it, close connection, etc.)
			break
		}

		// Process the message based on its type
		messageType := MessageType(buffer[0]) // Assuming the first byte indicates the message type
		switch messageType {
		case MessageTypeFrameUpdate:
			// Handle frame update request
			ci.handleFrameUpdate()
		case MessageTypeInputEvent:
			// Handle input event
			ci.handleInputEvent(buffer[1:n]) // Pass the rest of the buffer as the input event data
		default:
			// Handle unknown message type (e.g., log it, ignore it, etc.)
		}
	}
}

// Input message formats:
// - Touch event: [MessageTypeInputEvent(byte), InputTypeTouch(byte), x(int16), y(int16)]
// - Swipe event: [MessageTypeInputEvent(byte), InputTypeSwipe(byte), direction(int8), distance(int16)]
// - Button event: [MessageTypeInputEvent(byte), InputTypeButton(byte), buttonId(int16), state(int16)]
// - Rotary event: [MessageTypeInputEvent(byte), InputTypeRotary(byte), rotation(int32)]

type InputType byte

const (
	InputTypeTouch  InputType = 1
	InputTypeSwipe  InputType = 2
	InputTypeButton InputType = 3
	InputTypeRotary InputType = 4
)

func (ci *ConnectionInstance) handleInputEvent(data []byte) {

	// Parse the input event data and call the appropriate UI instance input function
	inputType := InputType(data[0]) // Assuming the first byte indicates the input type

	switch InputType(inputType) {
	case InputTypeTouch:
		x := int(binary.LittleEndian.Uint16(data[1:3])) // Read x coordinate (2 bytes)
		y := int(binary.LittleEndian.Uint16(data[3:5])) // Read y coordinate (2 bytes)
		ci.uiInstance.OnTouch(x, y)

	case InputTypeSwipe:
		direction := int(int8(data[1]))                        // Read direction (1 byte)
		distance := int(binary.LittleEndian.Uint16(data[2:4])) // Read distance (2 bytes)
		ci.uiInstance.OnSwipe(direction, distance)

	case InputTypeButton:
		buttonId := int(binary.LittleEndian.Uint16(data[1:3])) // Read button ID (2 bytes)
		state := int(binary.LittleEndian.Uint16(data[3:5]))    // Read button state (2 bytes)
		ci.uiInstance.OnButton(buttonId, state)

	case InputTypeRotary:
		rotation := int(binary.LittleEndian.Uint32(data[1:5])) // Read rotation (4 bytes)
		ci.uiInstance.OnRotary(rotation)

	default:
		// Handle unknown input type (e.g., log it, ignore it, etc.)
	}
}

func (ci *ConnectionInstance) handleFrameUpdate() {
	//	prev, curr := ci.uiInstance.Render()

	// Encode the frame data

	// Send the frame data to the client
}
