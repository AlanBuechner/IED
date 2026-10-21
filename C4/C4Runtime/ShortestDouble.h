#pragma once

// Room the shortest text is written into: at most 24 bytes of text, the rest taken by whole-word stores.
constexpr int kShortestDoubleRoom = 32;

// The shortest text that reads back as the finite `value` (`0.1`, `1000`, `-2.5e-07`) into `text`, which has room for
// kShortestDoubleRoom bytes: its length, without a terminator.
int WriteShortestDouble(char* text, double value);
