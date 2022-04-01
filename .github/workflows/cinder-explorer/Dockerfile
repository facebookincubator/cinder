FROM ghcr.io/facebookincubator/cinder/python-build-env:latest

RUN dnf install -y python3

# Build Cinder as root so the runtime is not modifiable from user code
# RUN git clone https://github.com/facebookincubator/cinder
COPY --chmod=r . cinder
WORKDIR cinder
WORKDIR build
RUN ../configure
RUN make -j$(nproc) VERBOSE=1
RUN chmod +x python

# Run server as appuser
RUN groupadd -g 1001 appuser
RUN useradd --create-home --shell /bin/bash --gid appuser appuser
USER appuser
WORKDIR /home/appuser
CMD /cinder/Tools/scripts/ir_viz/gen.py explorer --port 8000 --runtime /cinder/build/python
