FROM ghcr.io/facebookincubator/cinder/python-build-env:latest

RUN dnf install -y python3

# Build Cinder
# RUN git clone https://github.com/facebookincubator/cinder
COPY . cinder
WORKDIR cinder
RUN mkdir -p build
WORKDIR build
RUN ../configure
RUN make -j$(nproc) VERBOSE=1

# Run server
WORKDIR ..
RUN mkdir json
CMD ./Tools/scripts/ir_viz/gen.py explorer --port 8000 --runtime /cinder/build/python
