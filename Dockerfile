# crypt.c — multi-stage build.
# the builder needs gcc and xxd. the runtime needs nothing.

FROM debian:bookworm-slim AS builder
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential xxd ca-certificates \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN make

FROM debian:bookworm-slim AS runtime
RUN useradd -r -u 1000 -m crypt
COPY --from=builder /src/build/crypt /usr/local/bin/crypt
USER crypt
ENV PORT=8080
EXPOSE 8080
ENTRYPOINT ["/usr/local/bin/crypt"]
