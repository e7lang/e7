// "Did you mean ...?" suggestions for compiler diagnostics.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace e7 {

// Optimal string alignment distance (Levenshtein + adjacent transpositions); case differences
// cost half (scaled by 2 to stay integral).
inline size_t editDistance2(const std::string& a, const std::string& b) {
  size_t n = a.size(), m = b.size();
  std::vector<std::vector<size_t>> d(n + 1, std::vector<size_t>(m + 1));
  for (size_t i = 0; i <= n; i++) d[i][0] = 2 * i;
  for (size_t j = 0; j <= m; j++) d[0][j] = 2 * j;
  for (size_t i = 1; i <= n; i++)
    for (size_t j = 1; j <= m; j++) {
      size_t sub = a[i - 1] == b[j - 1] ? 0
                   : tolower((unsigned char)a[i - 1]) == tolower((unsigned char)b[j - 1]) ? 1 : 2;
      size_t v = std::min({d[i - 1][j] + 2, d[i][j - 1] + 2, d[i - 1][j - 1] + sub});
      if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[i - 2][j - 2] + 2);
      d[i][j] = v;
    }
  return d[n][m];
}

// The candidate closest to `name`, or "" when none is close enough.
inline std::string closestName(const std::string& name, const std::vector<std::string>& candidates) {
  if (name.empty() || name.size() > 64) return "";
  std::string best;
  size_t bestD = (size_t)-1;
  // allowed distance: about a third of the length (at least one edit)
  // (very short names only match when they differ in case alone)
  size_t limit = name.size() < 3 ? 1 : 2 * std::max<size_t>(1, (name.size() + 1) / 3);
  for (const std::string& c : candidates) {
    if (c == name || c.empty() || c[0] == '<' || c.size() > 64) continue;
    size_t lenDiff = c.size() > name.size() ? c.size() - name.size() : name.size() - c.size();
    if (2 * lenDiff > limit) continue;
    size_t d = editDistance2(name, c);
    if (d <= limit && (d < bestD || (d == bestD && c < best))) {
      best = c;
      bestD = d;
    }
  }
  return best;
}

// "; did you mean 'x'?" or "".
inline std::string didYouMean(const std::string& name, const std::vector<std::string>& candidates) {
  std::string b = closestName(name, candidates);
  return b.empty() ? "" : "; did you mean '" + b + "'?";
}

// Hint for an import of a name that e7:crypto no longer has: legacy algorithms were removed in 0.1, so
// E7 offers only modern ones; an app that must talk to a legacy protocol registers a host function.
inline std::string removedBuiltinHint(const std::string& module, const std::string& name) {
  if (module != "e7:crypto") return "";
  if (name == "crc32") return "; crc32 is in 'e7:encoding'";
  static const char* removed[] = {"md5", "sha1", "sha224", "sha3_224", "sha3_256", "sha3_384", "sha3_512",
                                  "keccak256", "aesCbcEncrypt", "aesCbcDecrypt", "aesCtr", "ecdsaSign",
                                  "ecdsaVerify", "ecdsaPublicKey", "rsaSign", "rsaVerify"};
  for (const char* r : removed)
    if (name == r)
      return "; e7:crypto has only modern algorithms (SHA-256/384/512, HMAC, HKDF, PBKDF2, AES-GCM, "
             "ChaCha20-Poly1305, Ed25519, X25519): register a host function for legacy ones";
  return "";
}

// Exported names of the builtin modules.
inline const std::vector<std::string>& builtinModuleExports(const std::string& module) {
  static const std::vector<std::string> encoding = {
      "hexEncode", "hexDecode", "base64Encode", "base64Decode", "base64UrlEncode", "base64UrlDecode",
      "base32Encode", "base32Decode", "utf8Encode", "utf8Decode", "pemDecode", "pemEncode", "concatBytes",
      "bytesEqual", "crc32"};
  static const std::vector<std::string> crypto = {
      "KeyHandle", "HashName", "sha256", "sha384", "sha512", "hash", "hmac", "hkdf", "pbkdf2", "randomBytes",
      "timingSafeEqual", "aesGcmEncrypt", "aesGcmDecrypt", "chacha20Poly1305Encrypt", "chacha20Poly1305Decrypt",
      "ed25519PublicKey", "ed25519Sign", "ed25519Verify", "x25519"};
  static const std::vector<std::string> test = {"assert", "assertEqual", "assertNotEqual", "assertClose",
                                                "assertThrows", "fail", "later", "laterReject"};
  static const std::vector<std::string> json = {"tryParse", "lastError"};
  static const std::vector<std::string> none;
  if (module == "e7:encoding") return encoding;
  if (module == "e7:crypto") return crypto;
  if (module == "e7:test") return test;
  if (module == "e7:json") return json;
  return none;
}

inline const std::vector<std::string>& builtinModuleNames() {
  static const std::vector<std::string> names = {"e7:crypto", "e7:encoding", "e7:json", "e7:test"};
  return names;
}

// The error suffix for a missing export of a builtin module: a correction, else the list of its exports (an
// agent that guesses names one compile at a time gets them all at once).
inline std::string builtinExportHint(const std::string& module, const std::string& name) {
  std::string hint = removedBuiltinHint(module, name);
  if (!hint.empty()) return hint;
  const auto& names = builtinModuleExports(module);
  std::string d = didYouMean(name, names);
  if (!d.empty()) return d;
  std::string all;
  for (auto& n : names) all += (all.empty() ? "" : ", ") + n;
  return "; it exports " + all;
}

}  // namespace e7
