FROM ghcr.io/facebookincubator/cinder/python-build-env:latest

COPY --chmod=r . cinder
WORKDIR cinder
WORKDIR build
RUN ../configure
RUN make -j$(nproc) VERBOSE=1
