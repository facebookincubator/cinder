# If updating this please also upload a new version of the container image.
# Details in https://fburl.com/wiki/0rzdo9ht

FROM fedora:32

LABEL org.opencontainers.image.source https://github.com/facebookincubator/cinder

RUN dnf install -y g++ git zlib-devel bzip2 bzip2-devel readline-devel sqlite \
                   sqlite-devel openssl-devel xz xz-devel libffi-devel findutils
