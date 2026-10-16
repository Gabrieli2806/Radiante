#pragma once

#include <ostream>
#include <string>

// The renderer's log. Lines written here are kept for the Java side, which moves them into the game's log
// (latest.log), so a player's log explains a problem such as a black world. They are copied to the console too while
// logging is on (the Debug logging option, on by default). Each thread writes through its own stream and lines reach
// the log whole, so threads that log at the same time do not mix their text.
namespace radiante {

void setLoggingEnabled(bool enabled);

bool loggingEnabled();

// Diagnostics: kept and copied to the console while logging is on, discarded while it is off.
std::ostream &out();

// Errors: always kept, copied to the console while logging is on.
std::ostream &err();

// The error lines written since the last call, each ending in '\n'.
std::string drainErrors();

// The diagnostic lines written since the last call, each ending in '\n'.
std::string drainInfo();

} // namespace radiante
