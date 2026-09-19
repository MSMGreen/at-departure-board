#pragma once
#include <ArduinoJson.h>

// One GET against the AT APIs: pinned TLS, the subscription-key header, the
// chunked/content-length branch, and a filtered streaming parse.

constexpr int AT_TRANSPORT_ERROR = -1;  // DNS, TLS, socket, timeout
constexpr int AT_PARSE_ERROR = -2;      // reply arrived but would not parse

// Returns the HTTP status when the server replied, otherwise one of the
// negatives above. `doc` is only populated on 200.
int at_get(const char* url, JsonDocument& doc, const JsonDocument& filter);
