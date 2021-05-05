FROM fedora:32
RUN dnf -y update
RUN dnf -y install git
RUN dnf -y groupinstall "Development Tools" "Development Libraries"
RUN git clone https://github.com/facebookincubator/cinder.git
WORKDIR /cinder
RUN /cinder/oss-build-and-test.sh
