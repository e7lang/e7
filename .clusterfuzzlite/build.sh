#!/bin/bash -eu
# ClusterFuzzLite / OSS-Fuzz build: compiler flags and $LIB_FUZZING_ENGINE come from the environment.
cmake -S "$SRC/e7" -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DE7_FUZZ=ON -DE7_BUILD_TESTS=OFF -DE7_BUILD_TOOLS=ON -DE7_NO_COMPUTED_GOTO=OFF
cmake --build build --target e7_fuzz_load e7_fuzz_compile e7_fuzz_natives e7c
cp build/fuzz/e7_fuzz_load build/fuzz/e7_fuzz_compile build/fuzz/e7_fuzz_natives "$OUT/"

# seed corpora: E7 programs for the compiler, compiled modules (+ known-malicious ones) for the loader
mkdir -p seeds-src seeds
i=0
for f in $(find "$SRC/e7/tests/cases" "$SRC/e7/examples" -name '*.ts'); do i=$((i+1)); cp "$f" "seeds-src/$i.ts"; done
(cd seeds-src && zip -q "$OUT/e7_fuzz_compile_seed_corpus.zip" ./*)
for f in $(find "$SRC/e7/tests/cases/lang" "$SRC/e7/tests/cases/lib" -name '*.ts'); do
  out="seeds/$(basename "$f" .ts).e7b"
  if build/e7c "$f" -o "$out" 2>/dev/null; then
    # the loader target treats an odd first byte as "repair the checksum"; prefix 0 = load as-is
    { printf '\x00'; cat "$out"; } > "$out.in" && rm "$out"
  fi
done
cp "$SRC"/e7/tests/malicious/*.e7b seeds/ 2>/dev/null || true
(cd seeds && zip -q "$OUT/e7_fuzz_load_seed_corpus.zip" ./*)
cp "$SRC/e7/fuzz/e7.dict" "$OUT/e7_fuzz_compile.dict"
# natives: crafted operation streams (fuzz/natives_seeds.py), one per native and property
(cd "$SRC/e7/fuzz/seeds/natives" && zip -q "$OUT/e7_fuzz_natives_seed_corpus.zip" ./*)
cp "$SRC/e7/fuzz/natives.dict" "$OUT/e7_fuzz_natives.dict"
