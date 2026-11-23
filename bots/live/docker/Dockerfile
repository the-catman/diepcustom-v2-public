# The bots server, its brain and a Cloudflare quick tunnel, built from the packed source (bots/live/pack.sh).
FROM ubuntu:24.04
ARG TARGETARCH=amd64
ENV DEBIAN_FRONTEND=noninteractive PYTHONUNBUFFERED=1
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build python3 python3-dev python3-venv pybind11-dev zlib1g-dev curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /venv \
    && /venv/bin/pip install --no-cache-dir torch --index-url https://download.pytorch.org/whl/cpu \
    && /venv/bin/pip install --no-cache-dir numpy
RUN curl -fsSL -o /usr/local/bin/cloudflared \
        https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-${TARGETARCH} \
    && chmod +x /usr/local/bin/cloudflared
COPY app /app
RUN cmake -S /app/bots/live -B /app/bots/live/build -G Ninja && cmake --build /app/bots/live/build --target bots-server \
    && cmake -S /app/bots/env -B /app/bots/env/build -G Ninja && cmake --build /app/bots/env/build
COPY start.sh /start.sh
CMD ["bash", "/start.sh"]
