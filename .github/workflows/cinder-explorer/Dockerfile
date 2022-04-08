FROM ghcr.io/facebookincubator/cinder:latest

RUN dnf install -y python3

# Run server as appuser
RUN groupadd -g 1001 appuser
RUN useradd --create-home --shell /bin/bash --gid appuser appuser
USER appuser
WORKDIR /home/appuser
CMD /cinder/Tools/scripts/ir_viz/gen.py explorer --port 8000 --runtime /cinder/build/python
