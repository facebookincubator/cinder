FROM registry.fedoraproject.org/fedora:32
RUN dnf --assumeyes --refresh group install "Development Tools" "Development Libraries"
RUN dnf clean all
ADD . /cinder
RUN /cinder/oss-build-and-test.sh
