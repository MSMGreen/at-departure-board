#include "at_client.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "at_ca.h"
#include "dechunk.h"
#include "secrets.h"

namespace {

constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t READ_TIMEOUT_MS = 10000;

// Dechunker's source: block until the socket has a byte, or give up.
int read_stream(void* ctx) {
  Stream* s = static_cast<Stream*>(ctx);
  const uint32_t t0 = millis();
  while (s->available() == 0) {
    if (millis() - t0 > READ_TIMEOUT_MS) return -1;
    delay(1);
  }
  return s->read();
}

}  // namespace

int at_get(const char* url, JsonDocument& doc, const JsonDocument& filter) {
  WiFiClientSecure client;
  client.setCACert(AT_ROOT_CA);  // never setInsecure(): spec 8
  client.setTimeout(READ_TIMEOUT_MS / 1000);

  HTTPClient http;
  http.setReuse(false);
  http.setConnectTimeout(CONNECT_TIMEOUT_MS);
  http.setTimeout(READ_TIMEOUT_MS);
  if (!http.begin(client, url)) return AT_TRANSPORT_ERROR;
  http.addHeader("Ocp-Apim-Subscription-Key", AT_API_KEY);

  const int status = http.GET();
  if (status <= 0) {
    char err[128];
    client.lastError(err, sizeof err);
    Serial.printf("at_get: transport error %d, TLS/socket detail: %s\n", status, err);
    http.end();
    return AT_TRANSPORT_ERROR;
  }
  if (status != HTTP_CODE_OK) {
    http.end();
    return status;
  }

  // Branch on what the server actually sent, never on which endpoint we think
  // we called: the GTFS API is chunked, the realtime API is not, and getting
  // this wrong fails silently in both directions.
  Stream& raw = http.getStream();
  DeserializationError err;
  if (http.getSize() < 0) {
    Dechunker dechunked(read_stream, &raw);
    err = deserializeJson(doc, dechunked, DeserializationOption::Filter(filter));
    if (!err && dechunked.failed()) err = DeserializationError::IncompleteInput;
  } else {
    err = deserializeJson(doc, raw, DeserializationOption::Filter(filter));
  }
  http.end();
  return err ? AT_PARSE_ERROR : status;
}
