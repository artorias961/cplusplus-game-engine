# The Linux CI job, as a container you can run on Windows before pushing.
#
# The same starting point as `.github/workflows/ci.yml`: the Ubuntu release the
# Linux job runs on, plus exactly the packages it installs. Keep the two in
# step — the whole value of this file is that a build which passes here passes
# there, and that stops being true the moment either one moves.
#
# Built once and cached; `verify.bat linux` only pays for this the first time.
FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y build-essential cmake pkg-config libsdl2-dev libsdl2-image-dev && rm -rf /var/lib/apt/lists/*

WORKDIR /src
