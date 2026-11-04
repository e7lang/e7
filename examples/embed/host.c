#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e7/e7.h"

static unsigned char* read_file(const char* path, size_t* len) {
  FILE* f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END); *len = (size_t)ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char* buf = malloc(*len);
  if (buf && fread(buf, 1, *len, f) != *len) { free(buf); buf = NULL; }
  fclose(f);
  return buf;
}

int main(void) {
  e7_config cfg;
  e7_config_init(&cfg);
  cfg.memory_limit = 32u << 20;

  /* Optional: if release.pub exists, only run modules signed by that key (see README, "Optional:
     sign your modules"). Without it, any valid .e7b runs. */
  uint8_t pub[32];
  size_t hexlen;
  unsigned char* hex = read_file("release.pub", &hexlen);
  if (hex && hexlen >= 64) {
    for (int i = 0; i < 32; i++) sscanf((const char*)hex + 2 * i, "%2hhx", &pub[i]);
    cfg.require_signature = 1;
    cfg.trusted_keys = pub;
    cfg.num_trusted_keys = 1;
  }
  free(hex);
  e7_vm* vm = e7_vm_new(&cfg);

  size_t len; unsigned char* code = read_file("sign.e7b", &len);
  e7_module* mod;
  if (!code || e7_load(vm, code, len, &mod) != E7_OK) { fprintf(stderr, "load: %s\n", code ? e7_last_error(vm) : "cannot read sign.e7b"); return 1; }
  free(code);

  e7_value args[2];
  args[0].type = E7_STRING; args[0].v.str.ptr = "amount=100&currency=CNY"; args[0].v.str.len = 23;
  args[1].type = E7_STRING; args[1].v.str.ptr = "s3cr3t";                  args[1].v.str.len = 6;
  e7_value ret;
  e7_set_budget(vm, 10000000);        /* stop runaway scripts */
  if (e7_call(vm, mod, "sign", args, 2, &ret) != E7_OK) { fprintf(stderr, "call: %s\n", e7_last_error(vm)); return 1; }
  printf("%.*s\n", (int)ret.v.str.len, ret.v.str.ptr);
  e7_vm_free(vm);
  return 0;
}
