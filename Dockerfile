FROM skalenetwork/consensust_base:latest

RUN apt-get update
RUN apt-get install -y software-properties-common
RUN add-apt-repository ppa:ubuntu-toolchain-r/test
RUN apt-get install -y software-properties-common; sudo apt-add-repository universe; apt-get update; \
    apt-get install -y libprocps-dev gcc-11 g++-11 valgrind gawk sed libffi-dev ccache libgoogle-perftools-dev \
    yasm texinfo autotools-dev automake python3 python3-pip \
    libtool build-essential pkg-config autoconf wget git libargtable2-dev \
    libmicrohttpd-dev libhiredis-dev redis-server openssl libssl-dev doxygen idn2 \
    libgcrypt20-dev
    # python python-pip

RUN wget --no-check-certificate https://cmake.org/files/v3.21/cmake-3.21.0-linux-x86_64.sh && \
    chmod +x cmake-3.21.0-linux-x86_64.sh && \
    ./cmake-3.21.0-linux-x86_64.sh --skip-license --include-subdir && \
    ln -sf `pwd`/cmake-3.21.0-linux-x86_64/bin/* /usr/local/bin
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 11
RUN update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 11
RUN update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-11 11
RUN update-alternatives --install /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-dump-11 11
RUN update-alternatives --install /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-tool-11 11

COPY . /consensust
WORKDIR /consensust

ENV CC gcc-11
ENV CXX g++-11
ENV TARGET all
ENV TRAVIS_BUILD_TYPE Debug

RUN deps/clean.sh
RUN deps/build.sh DEBUG=1 PARALLEL_COUNT=$(nproc)

RUN rm -rf ./build/*
RUN cmake . -Bbuild -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON -DMICROPROFILE_ENABLED=0
RUN bash -c "cmake --build build -- -j$(nproc)"

ENTRYPOINT ["/consensust/scripts/start.sh"]
