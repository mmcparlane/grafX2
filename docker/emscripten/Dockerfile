FROM debian:stretch

RUN apt-get update -y && apt-get install -y \
    nano \
    wget \
    curl \
    git \
    make \
    cmake \
    autoconf \
    automake \
    libtool \
    pkg-config \
    python \
    nodejs \
    default-jre-headless

WORKDIR /var/build/

ENV CFLAGS="-O2 --closure 1" \
    LDFLAGS="-O2 --closure 1"

RUN git clone https://github.com/emscripten-core/emsdk.git
RUN cd emsdk && \
    ./emsdk install latest && \
    ./emsdk activate latest

# Cache necessary ports prior to building dependencies

RUN ["/bin/bash", "-c", \
     "source /var/build/emsdk/emsdk_env.sh && \
      export PYTHONPATH=$PYTHONPATH:$EMSCRIPTEN && \
      python -c 'from tools import shared, system_libs; \
        shared.Settings.USE_ZLIB=1; \
        shared.Settings.USE_LIBPNG=1; \
        shared.Settings.USE_FREETYPE=1; \
        shared.Settings.USE_SDL=2; \
        shared.Settings.USE_SDL_IMAGE=2; \
        shared.Settings.USE_SDL_TTF=2; \
        shared.Settings.USE_SDL_GFX=2; \
        system_libs.get_ports(shared.Settings)'"]

# Manually compile everything that has no port available.

# JPEG

RUN wget https://ijg.org/files/jpegsrc.v9c.tar.gz
RUN tar xvf jpegsrc.v9c.tar.gz
RUN ["/bin/bash", "-c", \
     "source /var/build/emsdk/emsdk_env.sh && \
      cd /var/build/jpeg-9c && \
      emconfigure ./configure && \
      emmake make install"]

# TIFF

RUN wget https://download.osgeo.org/libtiff/tiff-4.0.10.tar.gz
RUN tar xvf tiff-4.0.10.tar.gz
RUN ["/bin/bash", "-c", \
     "source /var/build/emsdk/emsdk_env.sh && \
      cd tiff-4.0.10 && \
      emconfigure ./configure \
        --with-zlib-include-dir=/root/.emscripten_cache/asmjs/ports-builds/zlib \
        --with-zlib-lib-dir=/root/.emscripten_cache/asmjs/ports-builds/zlib \
        --with-jpeg-include-dir=/usr/local/include \
        --with-jpeg-lib-dir=/usr/local/lib \
        CFLAGS=\"$CFLAGS -s USE_LIBPNG=1 -s USE_ZLIB=1\" \
        LDFLAGS=\"$LDFLAGS -s USE_LIBPNG=1 -s USE_ZLIB=1\" && \
        emmake make && emmake make install"]

# GrafX2
# Stored in a volume so users can override with local copy of the sources instead.
RUN git clone -b emscripten https://github.com/mmcparlane/grafX2.git /var/build/grafx2
VOLUME /var/build/grafx2

CMD ["/bin/bash", "-c", \
     "source /var/build/emsdk/emsdk_env.sh && \
      cd grafx2 && \
      cp -r share src/share && \
      cp share/grafx2/gfx2def.ini src/gfx2def.ini && \
      emmake make NOTTF=1 NOLUA=1 API=sdl2"]
