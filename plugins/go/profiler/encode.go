package profiler

import (
	"encoding/binary"
	"math"
)

// The ITP v1 event types this client sends (IED docs/16 "Event catalog").
const (
	evHello          = 0x01
	evCalibration    = 0x02
	evBye            = 0x03
	evStringDef      = 0x10
	evSrcLocDef      = 0x11
	evThreadDef      = 0x12
	evThreadName     = 0x13
	evZoneBegin      = 0x20
	evZoneBeginNamed = 0x21
	evZoneEnd        = 0x22
	evZoneText       = 0x23
	evZoneValue      = 0x24
	evTaskBegin      = 0x30
	evTaskEnd        = 0x31
	evTaskSuspend    = 0x32
	evTaskResume     = 0x33
	evFrameMark      = 0x40
	evFrameBegin     = 0x41
	evFrameEnd       = 0x42
	evPlotDef        = 0x50
	evPlotValue      = 0x51
	evMessage        = 0x52
	evPoolDef        = 0x60
	evAlloc          = 0x61
	evFree           = 0x62
	evGcBegin        = 0x68
	evGcEnd          = 0x69
)

const (
	threadChunk     = 0
	globalChunk     = 1
	protocolVersion = 1
)

func putVarint(b []byte, v uint64) []byte {
	for v >= 0x80 {
		b = append(b, byte(v)|0x80)
		v >>= 7
	}
	return append(b, byte(v))
}

func putZigzag(b []byte, v int64) []byte {
	return putVarint(b, uint64(v<<1)^uint64(v>>63))
}

func putText(b []byte, s string) []byte {
	b = putVarint(b, uint64(len(s)))
	return append(b, s...)
}

func putDouble(b []byte, f float64) []byte {
	return binary.LittleEndian.AppendUint64(b, math.Float64bits(f))
}

// putChunk frames body as one chunk: its length, kind, thread and the time its first delta is from.
func putChunk(out []byte, kind byte, thread uint64, base int64, body []byte) []byte {
	var room [21]byte
	head := append(room[:0], kind)
	head = putVarint(head, thread)
	head = putVarint(head, uint64(base))
	out = binary.LittleEndian.AppendUint32(out, uint32(len(head)+len(body)))
	out = append(out, head...)
	return append(out, body...)
}

// preamble is the stream's first 8 bytes: the magic, the protocol version and no flags.
func preamble() []byte {
	return []byte{'I', 'T', 'P', '1', protocolVersion, 0, 0, 0}
}
