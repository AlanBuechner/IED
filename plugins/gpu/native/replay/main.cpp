// flashbang-replay: the replayer (docs/22 "Replay"). With --rpc it serves ied-gpu/1 on stdio for IED's server: each
// message is `Content-Length: N\r\n[Binary-Length: M\r\n]\r\n` then N bytes of JSON-RPC 2.0 and M bytes of binary.
// The other forms run one request and print its result, for tests and for looking at a capture by hand:
//   flashbang-replay --rpc
//   flashbang-replay <capture> [--no-device] [--debug-layer] info|events|resources|messages
//   flashbang-replay <capture> state|calls <eid>
//   flashbang-replay <capture> timing [repeat]
//   flashbang-replay <capture> png <resource id> <eid> <out.png> [mip]
//   flashbang-replay <capture> history <resource id> <eid> <x> <y> [mip]
#include "session.h"

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <windows.h>

using namespace fb;
using namespace fb::replay;

namespace
{
	void LogToStderr(const char* line) {
		fprintf(stderr, "%s\n", line);
		fflush(stderr);
	}

	bool ReadExactly(void* buffer, size_t size) {
		u8* at = (u8*)buffer;
		while (size) {
			const size_t got = fread(at, 1, size, stdin);
			if (!got) return false;
			at += got;
			size -= got;
		}
		return true;
	}

	bool ReadHeaderLine(std::string& line) {
		line.clear();
		for (;;) {
			const int c = fgetc(stdin);
			if (c == EOF) return false;
			if (c == '\n') {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				return true;
			}
			line += (char)c;
		}
	}

	void Send(const std::string& json, const std::vector<u8>& binary) {
		std::string header = Format("Content-Length: %zu\r\n", json.size());
		if (!binary.empty()) header += Format("Binary-Length: %zu\r\n", binary.size());
		header += "\r\n";
		fwrite(header.data(), 1, header.size(), stdout);
		fwrite(json.data(), 1, json.size(), stdout);
		if (!binary.empty()) fwrite(binary.data(), 1, binary.size(), stdout);
		fflush(stdout);
	}

	int ServeRpc() {
		_setmode(_fileno(stdin), _O_BINARY);
		_setmode(_fileno(stdout), _O_BINARY);
		Session session;
		for (;;) {
			size_t length = 0;
			std::string line;
			bool any = false;
			while (ReadHeaderLine(line)) {
				any = true;
				if (line.empty()) break;
				if (_strnicmp(line.c_str(), "Content-Length:", 15) == 0) length = (size_t)strtoull(line.c_str() + 15, nullptr, 10);
			}
			if (!any) return 0;
			std::string body(length, '\0');
			if (!ReadExactly(body.data(), length)) return 0;
			JsonValue message;
			std::string error;
			if (!JsonValue::Parse(body.data(), body.size(), message, error)) {
				Log("a message that is not JSON: %s", error.c_str());
				continue;
			}
			const std::string method = message["method"].Str();
			const JsonValue& id = message["id"];
			const Progress progress = [&](const char* what, double fraction) {
				JsonWriter note;
				note.BeginObject().String("jsonrpc", "2.0").String("method", "progress").BeginObject("params");
				note.Raw("id", id.Dump()).String("what", what).Double("fraction", fraction).EndObject().EndObject();
				Send(note.Text(), {});
			};
			Answer answer = session.Handle(method, message["params"], progress);
			if (id.IsNull()) continue;
			JsonWriter reply;
			reply.BeginObject().String("jsonrpc", "2.0").Raw("id", id.Dump());
			if (answer.error.empty()) reply.Raw("result", answer.result);
			else reply.BeginObject("error").Int("code", -32000).String("message", answer.error).EndObject();
			reply.EndObject();
			Send(reply.Text(), answer.binary);
			if (method == "shutdown") return 0;
		}
	}

	int Usage() {
		fprintf(stderr, "usage: flashbang-replay --rpc | <capture> [--no-device] [--debug-layer] info|events|resources|messages|state <eid>|calls <eid>|timing [repeat]|png <id> <eid> <out.png> [mip]|history <id> <eid> <x> <y> [mip]\n");
		return 2;
	}
}

int main(int argc, char** argv) {
	SetLogSink(LogToStderr);
	if (argc >= 2 && !strcmp(argv[1], "--rpc")) return ServeRpc();
	if (argc < 3) return Usage();
	JsonWriter params;
	params.BeginObject().String("capture", argv[1]).BeginObject("options");
	int at = 2;
	for (; at < argc && argv[at][0] == '-'; ++at) {
		if (!strcmp(argv[at], "--no-device")) params.Bool("noDevice", true);
		else if (!strcmp(argv[at], "--debug-layer")) params.Bool("debugLayer", true);
		else if (!strcmp(argv[at], "--warp")) params.Bool("warp", true);
	}
	params.EndObject().EndObject();
	if (at >= argc) return Usage();
	JsonValue initialize;
	std::string error;
	JsonValue::Parse(params.Text().data(), params.Text().size(), initialize, error);
	Session session;
	Answer opened = session.Handle("initialize", initialize, nullptr);
	if (!opened.error.empty()) {
		fprintf(stderr, "error: %s\n", opened.error.c_str());
		return 1;
	}
	const std::string command = argv[at];
	auto run = [&](const char* method, const std::string& json) {
		JsonValue request;
		std::string problem;
		JsonValue::Parse(json.data(), json.size(), request, problem);
		return session.Handle(method, request, [](const char* what, double fraction) { fprintf(stderr, "%s %.0f%%\n", what, fraction * 100); });
	};
	Answer answer;
	if (command == "info") answer = opened;
	else if (command == "events" || command == "resources" || command == "messages") answer = run(command.c_str(), "{}");
	else if ((command == "state" || command == "calls") && at + 1 < argc) answer = run(command == "state" ? "state" : "apiCalls", Format("{\"eid\":%s}", argv[at + 1]));
	else if (command == "timing") answer = run("timing", Format("{\"repeat\":%s}", at + 1 < argc ? argv[at + 1] : "3"));
	else if (command == "png" && at + 3 < argc) {
		JsonWriter request;
		request.BeginObject().Uint("id", strtoull(argv[at + 1], nullptr, 10)).Uint("eid", strtoul(argv[at + 2], nullptr, 10)).String("path", argv[at + 3]);
		request.Uint("mip", at + 4 < argc ? strtoul(argv[at + 4], nullptr, 10) : 0).Uint("maxSize", 8192).EndObject();
		answer = run("png", request.Text());
	}
	else if (command == "history" && at + 4 < argc) {
		JsonWriter request;
		request.BeginObject().Uint("id", strtoull(argv[at + 1], nullptr, 10)).Uint("eid", strtoul(argv[at + 2], nullptr, 10)).Uint("x", strtoul(argv[at + 3], nullptr, 10));
		request.Uint("y", strtoul(argv[at + 4], nullptr, 10)).Uint("mip", at + 5 < argc ? strtoul(argv[at + 5], nullptr, 10) : 0).EndObject();
		answer = run("pixelHistory", request.Text());
	}
	else return Usage();
	if (!answer.error.empty()) {
		fprintf(stderr, "error: %s\n", answer.error.c_str());
		return 1;
	}
	fwrite(answer.result.data(), 1, answer.result.size(), stdout);
	fputc('\n', stdout);
	return 0;
}
