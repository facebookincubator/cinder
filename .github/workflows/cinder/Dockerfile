FROM ghcr.io/facebookincubator/cinder/python-build-env:latest as fedora-build-env

# The "build" image - where we copy over the source tree and compile
FROM fedora-build-env as build
ARG make_verbose
# Making the `-j` value a build-arg so the user can adjust concurrency
# If unspecified we use `nproc` (number of host cores), which is likely to fail
# on Macs (and Windows?) due to concurrent filesystem access issues
ARG make_jobs
ENV MAKE_JOBS="${make_jobs}" MAKE_VERBOSE="${make_verbose:-1}"
WORKDIR /cinder/build
COPY --chmod=r . /cinder/src
RUN cp /cinder/src/oss-cinder-test.sh /cinder/build/
RUN /cinder/src/configure --prefix=/cinder
RUN make -j${MAKE_JOBS:-$( nproc )} VERBOSE=$MAKE_VERBOSE

# The "install" image - install the built Python and discard the source and build trees
FROM build as install
WORKDIR /cinder/build
RUN make install
# Discarding symbols from the built binary makes to reduce size
RUN strip /cinder/bin/python3.8
# These libraries are very big and generally are not needed
RUN rm /cinder/lib/libpython3.8_static.a /cinder/lib/python3.8/config-3.8-x86_64-linux-gnu/libpython3.8_static.a
# The test dir is very large - throw it away to reduce image size further
RUN rm -rf /cinder/lib/python3.8/test
# Discard the source and build trees to reduce clutter
RUN rm -rf /cinder/src /cinder/build
WORKDIR /

# The "runtime" image - create slim image with the runtime and no build chain and tooling
FROM fedora:32 as runtime
COPY --from=install /cinder /cinder
ENTRYPOINT ["/cinder/bin/python3"]

# Cinder Explorer
FROM runtime as explorer
COPY --from=build /cinder/src/Tools /cinder/Tools
# Run server as appuser
RUN groupadd -g 1001 appuser
RUN useradd --create-home --shell /bin/bash --gid appuser appuser
USER appuser
WORKDIR /home/appuser
EXPOSE 8000
ENTRYPOINT ["/cinder/bin/python3", "/cinder/Tools/scripts/ir_viz/gen.py"]
CMD ["explorer", "--runtime", "/cinder/bin/python3", "--port", "8000"]
