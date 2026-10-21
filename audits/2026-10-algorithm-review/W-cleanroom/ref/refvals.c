/* Reference values from the RandomX library, through its public API only (randomx.h).
 * Agent W clean-room check. Usage:
 *   refvals cache   <key> <out.bin>          dump the whole Cache memory (RANDOMX_ARGON_MEMORY KiB)
 *   refvals items   <key> <item>...          print Dataset items (hex, 64 bytes each)
 *   refvals hash    <key> <v1|v2> <input>... print light-mode hashes (input given as text; "-" = empty)
 *   refvals hashhex <key> <v1|v2> <hexinput>... same, input given as hex
 *   refvals commit  <hexinput> <hexhash>     print randomx_calculate_commitment
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "randomx.h"

static void hex(const unsigned char *p, size_t n) {
	for (size_t i = 0; i < n; i++) printf("%02x", p[i]);
}
static size_t unhex(const char *s, unsigned char *out) {
	size_t n = strlen(s) / 2;
	for (size_t i = 0; i < n; i++) { unsigned v; sscanf(s + 2 * i, "%2x", &v); out[i] = (unsigned char)v; }
	return n;
}

static randomx_cache *mkcache(const char *key) {
	randomx_flags f = randomx_get_flags() & (RANDOMX_FLAG_JIT | RANDOMX_FLAG_ARGON2);
	randomx_cache *c = randomx_alloc_cache(f);
	if (!c) { fprintf(stderr, "alloc cache failed\n"); exit(1); }
	randomx_init_cache(c, key, strlen(key));
	return c;
}

int main(int argc, char **argv) {
	if (argc < 3) { fprintf(stderr, "bad usage\n"); return 1; }
	const char *cmd = argv[1];
	if (!strcmp(cmd, "commit")) {
		unsigned char in[4096], h[32], out[32];
		size_t n = unhex(argv[2], in); unhex(argv[3], h);
		randomx_calculate_commitment(in, n, h, out);
		hex(out, 32); printf("\n");
		return 0;
	}
	const char *key = argv[2];
	randomx_cache *cache = mkcache(key);
	if (!strcmp(cmd, "cache")) {
		FILE *f = fopen(argv[3], "wb");
		fwrite(randomx_get_cache_memory(cache), 1024, 262144, f);
		fclose(f);
	} else if (!strcmp(cmd, "items")) {
		randomx_dataset *ds = randomx_alloc_dataset(RANDOMX_FLAG_DEFAULT);
		if (!ds) { fprintf(stderr, "alloc dataset failed\n"); return 1; }
		for (int i = 3; i < argc; i++) {
			unsigned long item = strtoul(argv[i], 0, 0);
			randomx_init_dataset(ds, cache, item, 1);
			printf("item %lu ", item);
			hex((unsigned char *)randomx_get_dataset_memory(ds) + 64 * item, 64);
			printf("\n");
		}
		randomx_release_dataset(ds);
	} else if (!strcmp(cmd, "hash") || !strcmp(cmd, "hashhex")) {
		randomx_flags f = randomx_get_flags();
		if (!strcmp(argv[3], "v2")) f |= RANDOMX_FLAG_V2;
		randomx_vm *vm = randomx_create_vm(f, cache, NULL);
		if (!vm) { fprintf(stderr, "create vm failed\n"); return 1; }
		for (int i = 4; i < argc; i++) {
			unsigned char out[32], buf[4096];
			size_t n;
			if (!strcmp(cmd, "hashhex")) n = unhex(argv[i], buf);
			else if (!strcmp(argv[i], "-")) n = 0;
			else { n = strlen(argv[i]); memcpy(buf, argv[i], n); }
			randomx_calculate_hash(vm, buf, n, out);
			printf("%s %s ", argv[3], key);
			hex(out, 32);
			printf(" input_hex=");
			hex(buf, n);
			printf("\n");
		}
		randomx_destroy_vm(vm);
	}
	randomx_release_cache(cache);
	return 0;
}
