// What the plugin tells Tor and reads back (docs/29 "Tor"): the torrc it writes and the control port's replies - text
// only, so it is tested without a Tor.

include "bedrock.ch"

namespace Tor
{
	// The files of a start, with forward slashes.
	struct TorFiles
	{
		public Core::String state;    // the plugin's state folder: data/, the control port's file, the log
		public Core::String geoip;    // the bundle's data/geoip and data/geoip6; "" when the bundle has none
		public Core::String geoip6;
	}

	// The torrc: loopback SOCKS and control ports Tor picks, cookie authentication, its files under the state folder, and
	// exits only in `exitCountries` (two-letter codes) when it names any.
	Core::String TorrcText(const TorFiles& files, const Core::Array<Core::String>& exitCountries);
	// The port ControlPortWriteToFile wrote ("PORT=127.0.0.1:55146"); 0 when the text is not that.
	int32 ControlPortOf(Core::StringView text);
	// The port of a GETINFO net/listeners/socks reply line ("250-net/listeners/socks=\"127.0.0.1:55145\""); 0 if none.
	int32 SocksPortOf(Core::StringView reply);
	// PROGRESS and SUMMARY of a GETINFO status/bootstrap-phase reply line; false when it has no PROGRESS.
	bool BootstrapOf(Core::StringView reply, int32& progress, Core::String& summary);
	// `count` bytes as lowercase hex (the cookie AUTHENTICATE takes).
	Core::String Hex(const uint8* bytes, int64 count);
	// Tor's last `lines` warnings and errors in its log `text`, or its last `lines` lines when it has none - joined by
	// " | ", for an error message.
	Core::String LogTail(Core::StringView text, int32 lines);
}
