// Minimal JSON helper utilities (string search) to avoid full JSON lib

#include <Arduino.h>
#include <ctype.h>

#include "json_utils.h"

String jsonFindString(const String &body, const String &key, int from) {
  String needle = String("\"") + key + "\":";
  int k = body.indexOf(needle, from);
  if (k == -1) return String();
  // allow optional spaces after ':'
  int pos = k + needle.length();
  while (pos < (int)body.length() && (body[pos] == ' ')) pos++;
  if (pos >= (int)body.length() || body[pos] != '"') return String();
  int q1 = pos;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 == -1) return String();
  return body.substring(q1 + 1, q2);
}

