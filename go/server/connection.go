package remote_ui_server

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

type Connection struct {
	tcpConn    net.Conn
	uiInstance UIInstance
}

// Message Types
type MessageType uint16

const (
	MessageTypeClientInfo  MessageType = 0xC001
	MessageTypeFrameUpdate MessageType = 0xC002
	MessageTypeInputEvent  MessageType = 0xC003
)

// readFromTcp reads exactly len(buf) bytes from conn, retrying on short reads.
// Returns an error if the connection is closed before the buffer is filled.
func readFromTcp(conn net.Conn, buf []byte) (int, error) {
	total := 0
	for total < len(buf) {
		n, err := conn.Read(buf[total:])
		total += n
		if err != nil {
			return total, err
		}
	}
	return total, nil
}

// HandleConnection runs in a go-routine and manages
// the lifecycle of the connection, including receiving
// messages from the client, processing them, and sending
// responses back.
// It will loop indefinitely until the connection is closed
// by either the client or the server.
func (ci *Connection) HandleConnection() {

	// - Message size is always < 1280 bytes
	// - We want to re-use the same buffer for each message to avoid unnecessary allocations
	// - When we receive a request to render a frame, we know the client is in a state to
	//   receive the frame data, and so it will not send other messages until it receives
	//   the frame data, so we can safely use the same buffer for both reading messages and
	//   sending frame data.

	const headerSize = 4
	buffer := make([]byte, 1280)
	for {
		// Read the 4-byte header: [messageType(uint16 LE), messageLen(uint16 LE)]
		if _, err := readFromTcp(ci.tcpConn, buffer[:headerSize]); err != nil {
			break
		}
		messageType := MessageType(binary.LittleEndian.Uint16(buffer[0:2]))
		messageLen := int(binary.LittleEndian.Uint16(buffer[2:4]))

		// Read the message body (messageLen bytes) after the header
		if messageLen > 0 {
			if _, err := readFromTcp(ci.tcpConn, buffer[headerSize:headerSize+messageLen]); err != nil {
				break
			}
		}

		switch messageType {
		case MessageTypeClientInfo:
			// Handle client info message (e.g., parse client capabilities, etc.)
			// For now, we can ignore it or log it as needed.
			ci.handleClientInfo(buffer[headerSize : headerSize+messageLen])
		case MessageTypeFrameUpdate:
			ci.handleFrameUpdate(buffer[headerSize : headerSize+messageLen])
		case MessageTypeInputEvent:
			ci.handleInputEvent(buffer[headerSize : headerSize+messageLen])
		default:
			// Handle unknown message type (e.g., log it, ignore it, etc.)
		}
	}
}

// Input message formats:
// - Touch event: [InputTypeTouch(uint16), x(int16), y(int16)]
// - Swipe event: [InputTypeSwipe(uint16), direction(int8), distance(int16)]
// - Button event: [InputTypeButton(uint16), buttonId(int16), state(int16)]
// - Rotary event: [InputTypeRotary(uint16), rotation(int32)]

type InputType uint16

const (
	InputTypeTouch  InputType = 0x1201
	InputTypeSwipe  InputType = 0x1202
	InputTypeButton InputType = 0x1203
	InputTypeRotary InputType = 0x1204
)

func (ci *Connection) handleInputEvent(data []byte) {

	// Parse the input event data and call the appropriate UI instance input function
	inputType := InputType(binary.LittleEndian.Uint16(data[0:2])) // Assuming the first byte indicates the input type

	switch InputType(inputType) {
	case InputTypeTouch:
		x := int(binary.LittleEndian.Uint16(data[2:4])) // Read x coordinate (2 bytes)
		y := int(binary.LittleEndian.Uint16(data[4:6])) // Read y coordinate (2 bytes)
		ci.uiInstance.OnTouch(x, y)

	case InputTypeSwipe:
		direction := int(int8(data[2]))                        // Read direction (1 byte)
		distance := int(binary.LittleEndian.Uint16(data[3:5])) // Read distance (2 bytes)
		ci.uiInstance.OnSwipe(direction, distance)

	case InputTypeButton:
		buttonId := int(binary.LittleEndian.Uint16(data[2:4])) // Read button ID (2 bytes)
		state := int(binary.LittleEndian.Uint16(data[4:6]))    // Read button state (2 bytes)
		ci.uiInstance.OnButton(buttonId, state)

	case InputTypeRotary:
		rotation := int(binary.LittleEndian.Uint32(data[2:6])) // Read rotation (4 bytes)
		ci.uiInstance.OnRotary(rotation)

	default:
		// Handle unknown input type (e.g., log it, ignore it, etc.)
	}
}

func (ci *Connection) handleClientInfo(data []byte) {
	// Handle client info message (e.g., parse client capabilities, etc.)
	// For now, we can ignore it or log it as needed.

}

func (ci *Connection) handleFrameUpdate(data []byte) {
	//	prev, curr := ci.uiInstance.Render()

	// Encode the frame data

	// Send the frame data to the client
}
