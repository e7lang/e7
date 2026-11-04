// Date natives. E7 has no local time zone: every Date behaves as in JavaScript with TZ=UTC.
#include <chrono>
#include <cstdio>
#include <cstring>

#include "util.h"

namespace e7 {

static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static double timeClip(double t) {
  if (!std::isfinite(t) || std::fabs(t) > 8.64e15) return NAN;
  return std::trunc(t) + 0.0;
}
// days since 1970-01-01 for a proleptic Gregorian date (month 1..12)
static double daysFromCivil(double y, double m, double d) {
  y -= m <= 2;
  double era = std::floor(y / 400);
  double yoe = y - era * 400;
  double mp = std::fmod(m + 9, 12);
  double doy = std::floor((153 * mp + 2) / 5) + d - 1;
  double doe = yoe * 365 + std::floor(yoe / 4) - std::floor(yoe / 100) + doy;
  return era * 146097 + doe - 719468;
}
static void civilFromDays(double z, double& y, double& m, double& d) {
  z += 719468;
  double era = std::floor(z / 146097);
  double doe = z - era * 146097;
  double yoe = std::floor((doe - std::floor(doe / 1460) + std::floor(doe / 36524) - std::floor(doe / 146096)) / 365);
  y = yoe + era * 400;
  double doy = doe - (365 * yoe + std::floor(yoe / 4) - std::floor(yoe / 100));
  double mp = std::floor((5 * doy + 2) / 153);
  d = doy - std::floor((153 * mp + 2) / 5) + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  if (m <= 2) y += 1;
}
static double makeDay(double year, double month, double date) {
  if (!std::isfinite(year) || !std::isfinite(month) || !std::isfinite(date)) return NAN;
  double y = toInteger(year), m = toInteger(month), dt = toInteger(date);
  double ym = y + std::floor(m / 12);
  double mn = std::fmod(m, 12);
  if (mn < 0) mn += 12;
  return daysFromCivil(ym, mn + 1, 1) + dt - 1;
}
static double makeTime(double h, double mi, double s, double ms) {
  if (!std::isfinite(h) || !std::isfinite(mi) || !std::isfinite(s) || !std::isfinite(ms)) return NAN;
  return toInteger(h) * 3600000.0 + toInteger(mi) * 60000.0 + toInteger(s) * 1000.0 + toInteger(ms);
}
static double fromParts(const Slot* a) {
  double y = a[0].f;
  if (std::isfinite(y)) {
    double yi = toInteger(y);
    if (yi >= 0 && yi <= 99) y = 1900 + yi;
  }
  double day = makeDay(y, a[1].f, a[2].f);
  double tm = makeTime(a[3].f, a[4].f, a[5].f, a[6].f);
  return timeClip(day * 86400000.0 + tm);
}

struct Parts { double y, mo, d, h, mi, s, ms, wd; };
static Parts split(double t) {
  Parts p;
  double day = std::floor(t / 86400000.0);
  double ms = t - day * 86400000.0;
  civilFromDays(day, p.y, p.mo, p.d);
  p.h = std::floor(ms / 3600000.0);
  p.mi = std::floor(std::fmod(ms, 3600000.0) / 60000.0);
  p.s = std::floor(std::fmod(ms, 60000.0) / 1000.0);
  p.ms = std::fmod(ms, 1000.0);
  p.wd = std::fmod(day + 4, 7);
  if (p.wd < 0) p.wd += 7;
  return p;
}

static std::string yearStr(double y, bool iso) {
  char b[32];
  if (iso) {
    if (y >= 0 && y <= 9999) snprintf(b, sizeof b, "%04d", (int)y);
    else snprintf(b, sizeof b, "%c%06d", y < 0 ? '-' : '+', (int)std::fabs(y));
  } else {
    if (y >= 0) snprintf(b, sizeof b, "%04d", (int)y);
    else snprintf(b, sizeof b, "-%06d", (int)-y);
  }
  return b;
}

std::string dateToISO(double t) {
  Parts p = split(t);
  char b[64];
  snprintf(b, sizeof b, "-%02d-%02dT%02d:%02d:%02d.%03dZ", (int)p.mo, (int)p.d, (int)p.h, (int)p.mi, (int)p.s, (int)p.ms);
  return yearStr(p.y, true) + b;
}
std::string dateToString(double t) {
  if (t != t) return "Invalid Date";
  Parts p = split(t);
  char b[96];
  snprintf(b, sizeof b, "%s %s %02d %s %02d:%02d:%02d GMT+0000 (Coordinated Universal Time)", kDays[(int)p.wd],
           kMonths[(int)p.mo - 1], (int)p.d, yearStr(p.y, false).c_str(), (int)p.h, (int)p.mi, (int)p.s);
  return b;
}
static std::string dateToUTCString(double t) {
  if (t != t) return "Invalid Date";
  Parts p = split(t);
  char b[96];
  snprintf(b, sizeof b, "%s, %02d %s %s %02d:%02d:%02d GMT", kDays[(int)p.wd], (int)p.d, kMonths[(int)p.mo - 1],
           yearStr(p.y, false).c_str(), (int)p.h, (int)p.mi, (int)p.s);
  return b;
}

// ISO 8601 (ECMAScript date time string format) and the toString / toUTCString formats
static double parseDate(const std::string& s0) {
  std::string s = s0;
  while (!s.empty() && isspace((unsigned char)s.back())) s.pop_back();
  size_t i = 0;
  auto digits = [&](int n, double& out) -> bool {
    if (i + n > s.size()) return false;
    double v = 0;
    for (int k = 0; k < n; k++) {
      char c = s[i + k];
      if (c < '0' || c > '9') return false;
      v = v * 10 + (c - '0');
    }
    i += n;
    out = v;
    return true;
  };
  double y, mo = 1, d = 1, h = 0, mi = 0, sec = 0, ms = 0;
  bool isoOk = true;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    bool neg = s[i] == '-';
    i++;
    if (!digits(6, y)) isoOk = false;
    if (neg) { if (y == 0) isoOk = false; y = -y; }
  } else if (!digits(4, y)) {
    isoOk = false;
  }
  bool dateOnly = true;
  double offset = 0;
  if (isoOk && i < s.size() && s[i] == '-') {
    i++;
    if (!digits(2, mo)) isoOk = false;
    if (isoOk && i < s.size() && s[i] == '-') {
      i++;
      if (!digits(2, d)) isoOk = false;
    }
  }
  if (isoOk && i < s.size() && (s[i] == 'T' || s[i] == 't' || s[i] == ' ')) {
    dateOnly = false;
    i++;
    if (!digits(2, h) || i >= s.size() || s[i] != ':') isoOk = false;
    else {
      i++;
      if (!digits(2, mi)) isoOk = false;
      if (isoOk && i < s.size() && s[i] == ':') {
        i++;
        if (!digits(2, sec)) isoOk = false;
        if (isoOk && i < s.size() && (s[i] == '.' || s[i] == ',')) {
          i++;
          size_t st = i;
          double frac = 0, scale = 1;
          while (i < s.size() && isdigit((unsigned char)s[i])) {
            if (scale < 1000) { frac = frac * 10 + (s[i] - '0'); scale *= 10; }
            i++;
          }
          if (i == st) isoOk = false;
          ms = frac * (1000 / scale);
        }
      }
      if (isoOk && i < s.size()) {
        if (s[i] == 'Z' || s[i] == 'z') i++;
        else if (s[i] == '+' || s[i] == '-') {
          double sign = s[i] == '-' ? -1 : 1, oh, om;
          i++;
          if (!digits(2, oh)) isoOk = false;
          else {
            if (i < s.size() && s[i] == ':') i++;
            if (!digits(2, om)) isoOk = false;
            else offset = sign * (oh * 60 + om);
          }
        }
      }
    }
  }
  (void)dateOnly;
  if (isoOk && i == s.size()) {
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 24 || mi > 59 || sec > 59 || (h == 24 && (mi || sec || ms)))
      return NAN;
    double day = daysFromCivil(y, mo, d);  // like V8, day 29-31 overflow into the next month
    return timeClip(day * 86400000.0 + makeTime(h, mi, sec, ms) - offset * 60000.0);
  }
  // "Tue, 14 Nov 2023 22:13:20 GMT" / "Tue Nov 14 2023 22:13:20 GMT+0000 (...)"
  char wd[8], mon[8], tz[16] = "GMT";
  int dd, yy, hh, mm, ss;
  if (sscanf(s.c_str(), "%3s, %d %3s %d %d:%d:%d %15s", wd, &dd, mon, &yy, &hh, &mm, &ss, tz) >= 7 ||
      sscanf(s.c_str(), "%3s %3s %d %d %d:%d:%d %15s", wd, mon, &dd, &yy, &hh, &mm, &ss, tz) >= 7) {
    int mIdx = -1;
    for (int k = 0; k < 12; k++)
      if (strncmp(mon, kMonths[k], 3) == 0) mIdx = k;
    if (mIdx < 0) return NAN;
    double off = 0;
    if (strncmp(tz, "GMT", 3) == 0 && (tz[3] == '+' || tz[3] == '-') && strlen(tz) >= 8) {
      int v = atoi(tz + 4);
      off = (tz[3] == '-' ? -1 : 1) * ((v / 100) * 60 + v % 100);
    }
    return timeClip(daysFromCivil(yy, mIdx + 1, dd) * 86400000.0 + makeTime(hh, mm, ss, 0) - off * 60000.0);
  }
  return NAN;
}

static BoxObj* newDate(VM& vm, double t) {
  Slot v;
  v.f = timeClip(t);
  BoxObj* b = vm.newBox(VT_NUMBER, v);
  b->flags = 1;  // Date
  return b;
}
static BoxObj* D(VM& vm, Slot s) {
  Obj* o = s.r;
  if (!o || o->type != OT::Box || !(o->flags & 1)) vm.error("expected a Date");
  return static_cast<BoxObj*>(o);
}

E7_NATIVE(d_newNow) {
  E7_UNUSED;
  using namespace std::chrono;
  r->r = newDate(vm, (double)duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}
E7_NATIVE(d_fromNumber) { E7_UNUSED; r->r = newDate(vm, a[0].f); }
E7_NATIVE(d_fromString) { E7_UNUSED; r->r = newDate(vm, parseDate(strUtf8(S(vm, a[0])))); }
E7_NATIVE(d_copy) { E7_UNUSED; r->r = newDate(vm, D(vm, a[0])->v.f); }
E7_NATIVE(d_fromParts) { E7_UNUSED; r->r = newDate(vm, fromParts(a)); }
E7_NATIVE(d_UTC) { E7_UNUSED; r->f = fromParts(a); }
E7_NATIVE(d_parse) { E7_UNUSED; r->f = parseDate(strUtf8(S(vm, a[0]))); }
E7_NATIVE(d_getTime) { E7_UNUSED; r->f = D(vm, a[0])->v.f; }
E7_NATIVE(d_setTime) {
  E7_UNUSED;
  BoxObj* b = D(vm, a[0]);
  b->v.f = timeClip(a[1].f);
  r->f = b->v.f;
}
E7_NATIVE(d_get) {
  E7_UNUSED;
  double t = D(vm, a[0])->v.f;
  if (t != t) { r->f = NAN; return; }
  Parts p = split(t);
  switch ((int)a[1].f) {
    case 0: r->f = p.y; break;
    case 1: r->f = p.mo - 1; break;
    case 2: r->f = p.d; break;
    case 3: r->f = p.wd; break;
    case 4: r->f = p.h; break;
    case 5: r->f = p.mi; break;
    case 6: r->f = p.s; break;
    default: r->f = p.ms; break;
  }
}
E7_NATIVE(d_toISOString) {
  E7_UNUSED;
  double t = D(vm, a[0])->v.f;
  if (t != t) vm.error("RangeError: Invalid time value");
  r->r = vm.newStringAscii(dateToISO(t).c_str());
}
E7_NATIVE(d_toString) { E7_UNUSED; r->r = vm.newStringAscii(dateToString(D(vm, a[0])->v.f).c_str()); }
E7_NATIVE(d_toUTCString) { E7_UNUSED; r->r = vm.newStringAscii(dateToUTCString(D(vm, a[0])->v.f).c_str()); }
E7_NATIVE(d_toDateString) {
  E7_UNUSED;
  std::string s = dateToString(D(vm, a[0])->v.f);
  r->r = vm.newStringAscii(s == "Invalid Date" ? s.c_str() : s.substr(0, 15).c_str());
}
E7_NATIVE(d_toTimeString) {
  E7_UNUSED;
  std::string s = dateToString(D(vm, a[0])->v.f);
  r->r = vm.newStringAscii(s == "Invalid Date" ? s.c_str() : s.substr(16).c_str());
}

void registerDateNatives(VM& vm) {
  vm.registerNative("Date.newNow", ">R", d_newNow);
  vm.registerNative("Date.fromNumber", "F>R", d_fromNumber);
  vm.registerNative("Date.fromString", "R>R", d_fromString);
  vm.registerNative("Date.copy", "R>R", d_copy);
  vm.registerNative("Date.fromParts", "FFFFFFF>R", d_fromParts);
  vm.registerNative("Date.UTC", "FFFFFFF>F", d_UTC);
  vm.registerNative("Date.parse", "R>F", d_parse);
  vm.registerNative("Date.getTime", "R>F", d_getTime);
  vm.registerNative("Date.setTime", "RF>F", d_setTime);
  vm.registerNative("Date.get", "RF>F", d_get);
  vm.registerNative("Date.toISOString", "R>R", d_toISOString);
  vm.registerNative("Date.toString", "R>R", d_toString);
  vm.registerNative("Date.toUTCString", "R>R", d_toUTCString);
  vm.registerNative("Date.toDateString", "R>R", d_toDateString);
  vm.registerNative("Date.toTimeString", "R>R", d_toTimeString);
}

}  // namespace e7
