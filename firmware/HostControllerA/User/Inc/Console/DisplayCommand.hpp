#pragma once

#include <Display/Display.hpp>

namespace Console {

enum class CommandResult {
    NotHandled,
    Ok,
    Busy,
    Unavailable,
    InvalidArgument,
};

// The 'display' commands: content, blink and level of a board, a test
// pattern, and the pass table behind the levels. Replies itself on success
// ('OK display ...') and for a board that does not answer; see
// Docs/Console.md#display.
//
// An optional target after 'display' picks the board: none is the local
// board, 0x10-0x15 the board at that address (the local one at the host's
// own), 'all' every board. 'passes' is the local board's only.
//
//   display show <n> <value>     numeric n: a number, HH:MM, ? or blank
//   display row <r> <cells>      matrix row r, the payload's cells 0-3 a-c * ? (. is off)
//   display blink <n> off|colon|all
//   display level <n> <0-3>
//   display test                 levels and blinking on every element
//   display clear                everything off, attributes plain
//   display passes [a b c d]     show or set the pass lengths, percent of a slot
CommandResult handleDisplayCommand(const char* line, Display::Display* display);

} // namespace Console
