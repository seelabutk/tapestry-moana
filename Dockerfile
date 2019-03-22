FROM ubuntu:bionic

RUN apt-get update && \ 
    apt-get install -y \
	libopenmpi-dev \
	libopenimageio-dev \
	pkg-config \
	make \
	cmake \
	build-essential \
	libz-dev \
	libtbb-dev \
	libglu1-mesa-dev \
	freeglut3-dev \
	libnetcdf-c++4-dev \
	xorg-dev \
        x11-apps \
	xauth \
	x11-xserver-utils \
	vim \
	libjpeg-dev \
	imagemagick \
	python3.7 \
        python3-pip \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /opt/
ADD ispc-v1.9.1-linux.tar.gz /opt/
RUN mv ispc-v1.9.1-linux ispc
WORKDIR /opt/ispc/
RUN update-alternatives --install /usr/bin/ispc ispc /opt/ispc/ispc 1

WORKDIR /opt/
ADD embree-3.5.0.x86_64.linux.tar.gz /opt/
RUN mv embree-3.5.0.x86_64.linux embree
WORKDIR /opt/embree/

WORKDIR /app/
COPY moana-ospray-demo-v1.2.tgz /tmp/
RUN tar xf /tmp/moana-ospray-demo-v1.2.tgz --strip-components 1

WORKDIR /app/source
RUN bash ./README.sh

WORKDIR /src/
COPY 1 1
RUN chown root:root -R 1 && cp -rv 1/* /app/ && rm -rf 1 && cd /app/source && bash ./README.sh

WORKDIR /src/
COPY 2 2
RUN chown root:root -R 2 && cp -rv 2/* /app/ && rm -rf 2 && cd /app/source && bash ./README.sh

WORKDIR /src/
COPY 3 3
RUN chown root:root -R 3 && cp -rv 3/* /app/ && rm -rf 3

WORKDIR /src/
COPY 4 4
RUN chown root:root -R 4 && cp -rv 4/* /app/ && rm -rf 4

WORKDIR /app/
RUN ln -sf /home/ahota/data/disney/moana/island-v1.1 island && \
    ln -sf /home/ahota/data/disney/moana /fast
COPY server .
