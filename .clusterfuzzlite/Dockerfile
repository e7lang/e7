FROM gcr.io/oss-fuzz-base/base-builder:v1
RUN apt-get update && apt-get install -y cmake ninja-build zip
COPY . $SRC/e7
WORKDIR $SRC/e7
COPY .clusterfuzzlite/build.sh $SRC/
