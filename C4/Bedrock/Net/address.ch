// Address: an IPv4 or IPv6 address and a port (D-65). A value: compare it, hash it, print it. A zero Address is not
// valid (Family::None). Its layout is the runtime's (C4Runtime/NetRuntimeC.h, c4_net_address).
//     Net::Address local = Net::Address::Loopback(8080);              // 127.0.0.1:8080
//     Net::Address parsed = Net::ParseAddress("[::1]:443");
//     Net::Resolution found = Net::Resolve("localhost", 80);           // every address the name has

include "../Container/array.ch"
include "../Text/string.ch"

namespace Net
{
	enum class Family { None = 0, IPv4 = 4, IPv6 = 6 }

	// The highest port; 0 asks the system for a free one when listening.
	const int32 kMaxPort = 65535;

	struct Address
	{
		public Family family;
		public int32 port;
		public uint8[16] bytes;   // network order; IPv4 uses the first four
		public uint32 scope;      // the interface of an IPv6 link-local address, else 0

		public static Address IPv4(uint8 a, uint8 b, uint8 c, uint8 d, int32 port);
		public static Address Loopback(int32 port) { return IPv4(127, 0, 0, 1, port); }     // 127.0.0.1
		public static Address Any(int32 port) { return IPv4(0, 0, 0, 0, port); }            // 0.0.0.0: every interface
		public static Address LoopbackV6(int32 port);                                       // ::1
		public static Address AnyV6(int32 port);                                            // ::

		public bool IsValid() const { return (family == Family::IPv4 || family == Family::IPv6) && port >= 0 && port <= kMaxPort; }
		public Address WithPort(int32 newPort) const;
		// "127.0.0.1:80", "[::1]:80", "[fe80::1%3]:80"; empty when not valid.
		public Core::String ToString() const;

		[operator("==")] public bool Equals(const Address& other) const;
		[operator("!=")] public bool NotEquals(const Address& other) const { return !Equals(other); }
		[operator("hash")] public uint64 Hash() const;
	}

	// A numeric address with an optional port: "10.0.0.1", "10.0.0.1:80", "::1", "[::1]:443", "[fe80::1%3]:80". Names are
	// not looked up (Resolve does that); an Address that is not valid when the text is not one.
	Address ParseAddress(const char* text);

	// What Resolve found: the addresses, in the order the system prefers them, or why there are none.
	struct Resolution
	{
		public Core::Array<Address> addresses;
		public Core::String error;

		public bool Found() const { return addresses.Count() > 0; }
	}

	// The addresses `host` (a name or a numeric address) has, each with `port`.
	Resolution Resolve(const char* host, int32 port);

	// The longest error text a socket keeps: the system's message and its code.
	const int32 kErrorBytes = 512;
}
