# The published image: a static divejson-raw and its notices, nothing else. Another image
# takes the binary with `COPY --from=ghcr.io/divejson/divejson-c:<version>
# /usr/local/bin/divejson-raw /usr/local/bin/divejson-raw`, and the notices with it.
FROM alpine:3.22 AS build

RUN apk add --no-cache build-base cmake linux-headers ninja

WORKDIR /src
COPY . .

# musl, so -static yields a binary with no loader and no libc to find at run time.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DDIVEJSON_LIBDIVECOMPUTER=ON -DDIVEJSON_TESTS=OFF -DDIVEJSON_STATIC=ON \
 && cmake --build build \
 && strip build/divejson-raw \
 && build/divejson-raw --version \
 && ! ldd build/divejson-raw 2>/dev/null

FROM scratch
COPY --from=build /src/build/divejson-raw /usr/local/bin/divejson-raw
COPY LICENSE NOTICE /usr/share/licenses/divejson-c/
COPY vendor/libdivecomputer/COPYING /usr/share/licenses/divejson-c/LGPL-2.1.txt
ENTRYPOINT ["/usr/local/bin/divejson-raw"]
