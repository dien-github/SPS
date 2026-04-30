# Use base image Ubuntu version 22.04
FROM ubuntu:22.04

# BUILD ARGUMENTS
ARG HOST_UID=1000

# Install mandatory packages and clear cache
RUN apt-get update && apt-get install -y --no-install-recommends \
    debianutils sed make binutils build-essential diffutils gcc g++ bash patch \
    gzip bzip2 perl tar cpio unzip rsync file bc findutils gawk wget \
    libncurses-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Create new user
# Check UID of where you store the Buildroot source code
RUN useradd -m -u ${HOST_UID} br-user

# Change to new user
USER br-user
WORKDIR /home/br-user