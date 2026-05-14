package remote_ui_server

// The Remote UI server, which listens for incoming TCP connections from clients,
// and creates a new instance for each connection. Each instance runs in its own
// go-routine, and manages the lifecycle of the connection, including receiving
// messages from the client, processing them, and sending responses back.

// A global map is declared that maps a client's MAC address to its corresponding
// instance implementation. Since a connection only interacts with an instance
// interface, we need to actually create interface implementations for each client,
// and then map them to the client's MAC address, so when a client connects, we can
// identify it and create the appropriate instance for it.

type InstanciateUserInterfaceFn func() UserInterface

// Maps Type to a function that creates an instance of the corresponding UI implementation.
var userInterfaceTypeToNewInstance = map[string]InstanciateUserInterfaceFn{
	"Weather UI": func() UserInterface { return NewWeatherUserInterface() },
}

// The Remote UI server when starting does the following:
// -
// - Load configuration
//   - each instance descriptor is associated with its Mac-Address, so when a
//     client connects, we can identify it and create the appropriate instance
//     for it.
// - Load Sprite and Font packs and hand-out to each instance, so they can render
//   the UI frames without having to load the assets themselves.
// -

type Server struct {
}
